# mcpxx Architecture

This document describes the internal design of the mcpxx library: how layers are organized,
how threads interact, how a message travels from the wire to a user handler, and why key
design decisions were made.

---

## Layer Diagram

```
+------------------------------------------------------------------+
|                        User Application                          |
|  McpServer / McpClient  (high-level API, tool/resource/prompt    |
|  registration, lifecycle management)                             |
+------------------------------------------------------------------+
              |                             ^
        register handlers            call results
              v                             |
+------------------------------------------------------------------+
|                           Router                                 |
|  Dispatches incoming requests to registered handlers.            |
|  Holds maps: method -> handler, tool name -> handler, etc.       |
+------------------------------------------------------------------+
              |                             ^
        decoded messages           encoded responses
              v                             |
+------------------------------------------------------------------+
|                           Session                                |
|  Tracks pending requests (id -> promise), handles initialize     |
|  handshake, manages cancellation tokens, progress callbacks.     |
|  Owns the Codec and the Transport.                               |
+------------------------------------------------------------------+
              |                             ^
          raw JSON              serialized JSON frames
              v                             |
+------------------------------------------------------------------+
|                           Codec                                  |
|  JSON-RPC 2.0 framing: serialize / deserialize Message structs.  |
|  Validates id, method, params, error shapes.                     |
+------------------------------------------------------------------+
              |                             ^
       byte buffers                  byte buffers
              v                             |
+------------------------------------------------------------------+
|                          Transport                               |
|  stdio: reads/writes newline-delimited JSON on stdin/stdout.     |
|  HTTP:  Streamable HTTP transport (SSE for server->client).      |
+------------------------------------------------------------------+
```

---

## Layer Descriptions

### Transport

The transport layer handles raw I/O. It exposes two async primitives:

- `async_read_message(handler)` — invokes handler with a raw `std::string` containing one
  complete JSON-RPC frame each time a full frame arrives.
- `async_write_message(str)` — writes a serialized frame to the output channel.

Two transport implementations are provided:

- **StdioTransport** — reads newline-delimited JSON from `stdin`, writes to `stdout`.
  Suitable for Claude Desktop integration and subprocess-based servers.
- **StreamableHttpTransport** — implements the MCP Streamable HTTP transport: GET requests
  open an SSE stream for server-initiated messages; POST requests carry client-initiated
  messages. A session cookie ties the two directions together.

### Codec

The codec layer translates between raw string frames and typed `Message` variants:

```cpp
using Message = std::variant<Request, Response, Notification, ErrorResponse>;
```

- `encode(Message) -> std::string` — serializes a message to a JSON-RPC 2.0 frame.
- `decode(std::string) -> Message` — parses a frame and validates its structure.

The codec does not do MCP-level validation; it only enforces JSON-RPC 2.0 shape.

### Session

The session layer owns one Transport and one Codec instance. It is responsible for:

- Running the read loop: continuously reading frames from Transport, decoding with Codec,
  then dispatching to the Router.
- Tracking in-flight requests: a `std::unordered_map<json_id, pending_call>` maps each
  outgoing request id to a `std::promise` so that `call()` callers can await the response.
- Sending requests and notifications: serializes via Codec, writes via Transport.
- Managing the MCP `initialize` handshake: negotiates protocol version and capabilities.
- Implementing cancellation: when `cancelled/` notifications arrive, the corresponding
  pending handler is signaled.
- Propagating progress notifications to user-registered callbacks.

### Router

The router maintains a registry of method handlers:

```
method string  ->  std::function<Message(Request)>
```

Specialized sub-registries exist for tools, resources, and prompts so that the generic
`tools/call`, `resources/read`, etc. handlers can delegate to named handlers efficiently
using `std::unordered_map`.

The router also enforces capability checks: if the remote peer did not advertise a
capability during `initialize`, the router rejects calls to methods that require it.

### Server / Client

`McpServer` and `McpClient` are the public-facing classes. They:

- Accept user-provided options (server info, capabilities, transport factory).
- Provide ergonomic registration methods: `add_tool`, `add_resource`, `add_prompt`, etc.
- Wrap Session and Router construction.
- Expose `serve_stdio()` / `serve_http()` for servers, and `connect_stdio()` /
  `connect_http()` for clients.

---

## Threading Model

```
Main thread
  |
  +-- McpServer::serve_stdio()
        |
        +-- Session::run()   <-- single-threaded event loop
              |
              +-- read_loop(): blocking read on Transport
              |
              +-- on_message(): decode + dispatch (called inline in read_loop)
              |
              +-- handler invocation:
                    * If handler is cheap  -> called inline
                    * If MCPXX_THREAD_POOL=ON -> posted to thread pool,
                      response sent when future resolves
```

By default mcpxx is fully single-threaded: the read loop, codec, router, and all handler
invocations run on the same thread. This eliminates locking overhead and makes handler
code safe to write without synchronization.

When the `MCPXX_THREAD_POOL` CMake option is enabled, a `std::thread` pool is created.
Long-running handlers (marked with `tool.run_async = true`) are posted to the pool. The
session collects their `std::future<CallToolResult>` and sends the response when the
future becomes ready, multiplexed on the event loop via a pipe-based wakeup.

Callbacks registered by the user (tool handlers, resource handlers, etc.) MUST NOT call
back into the Session from a thread-pool thread except through the thread-safe
`McpServer::send_notification()` method.

---

## Message Flow: Tool Call

Below is the complete path of a `tools/call` request from a connected client to the
tool handler and back.

```
Client                 Transport           Codec          Session        Router       Handler
  |                       |                  |               |              |             |
  | -- POST /mcp -------> |                  |               |              |             |
  |   {"jsonrpc":"2.0",   |                  |               |              |             |
  |    "id":1,            |                  |               |              |             |
  |    "method":          |                  |               |              |             |
  |    "tools/call",      |                  |               |              |             |
  |    "params":{...}}    |                  |               |              |             |
  |                       |                  |               |              |             |
  |                       | -- read_frame -> |               |              |             |
  |                       |                  | -- decode --> |              |             |
  |                       |                  |    Request{   |              |             |
  |                       |                  |     id=1,     |              |             |
  |                       |                  |     method=   |              |             |
  |                       |                  |     "tools/   |              |             |
  |                       |                  |      call"}   |              |             |
  |                       |                  |               | -- route --> |             |
  |                       |                  |               |              | -- invoke-->|
  |                       |                  |               |              |   handler(  |
  |                       |                  |               |              |    params)  |
  |                       |                  |               |              |         |   |
  |                       |                  |               |              |  <-- result |
  |                       |                  |               | <-- result --|             |
  |                       |                  | <-- encode -- |              |             |
  |                       | <-- write_frame--|               |              |             |
  | <-- HTTP response --- |                  |               |              |             |
  |   {"jsonrpc":"2.0",   |                  |               |              |             |
  |    "id":1,            |                  |               |              |             |
  |    "result":{...}}    |                  |               |              |             |
```

For stdio transport, the POST step is replaced by a newline-delimited write to stdin, and
the response is a newline-delimited write to stdout.

---

## Design Decisions

### Why nlohmann/json?

nlohmann/json is the de-facto standard JSON library for modern C++. It has a stable API,
excellent documentation, header-only mode for simple embedding, and acceptable performance
for MCP message sizes. For scenarios where parse latency is critical, the session layer can
be swapped to use simdjson via a compile-time policy template.

### Why single-threaded by default?

MCP servers are typically I/O bound subprocess companions. A single-threaded event loop
avoids all synchronization complexity, makes handler code easy to reason about, and
delivers low latency because there is no lock contention. Thread-pool support is opt-in for
the minority of use cases that have CPU-bound tools.

### Why std::variant for Message?

A tagged union expressed as `std::variant` lets the compiler enforce exhaustive handling
via `std::visit`. This eliminates entire classes of bugs (unhandled message types, missing
response paths) at compile time rather than at runtime.

### Why FetchContent for dependencies?

Requiring users to pre-install exact versions of nlohmann/json, asio, etc. creates
packaging friction. FetchContent pins the exact revision used in CI, guaranteeing
reproducible builds across environments. A `MCPXX_USE_SYSTEM_DEPS` option is provided for
distributions that prefer system packages.

### Error handling strategy

All public API methods return `std::expected<T, McpError>` (C++23) or throw
`McpException` depending on a compile-time policy. Internal code uses `std::expected`
exclusively to avoid exception overhead on hot paths. User handlers may throw; the session
layer catches all exceptions from handlers and converts them to JSON-RPC error responses.
