# mcpxx API Reference

This document describes the public C++ API of the mcpxx library. All symbols live in the
`mcp` namespace unless noted otherwise.

Include the single umbrella header to access the full API:

```cpp
#include <mcp/mcp.hpp>
```

---

## Table of Contents

1. [Key Types](#key-types)
2. [McpServer](#mcpserver)
3. [McpClient](#mcpclient)
4. [Error Handling](#error-handling)
5. [Transport Factories](#transport-factories)

---

## Key Types

### Implementation

```cpp
struct Implementation {
    std::string name;
    std::optional<std::string> title;   // human-readable display name
    std::string version;
};
```

Describes a server or client identity. Sent during the `initialize` handshake.

---

### ToolDefinition

```cpp
struct ToolDefinition {
    std::string name;
    std::optional<std::string> description;
    nlohmann::json input_schema;           // JSON Schema object
    std::optional<nlohmann::json> annotations;
    bool run_async = false;                // post handler to thread pool if true
};
```

Passed to `McpServer::add_tool()`. `input_schema` must be a valid JSON Schema object
with `"type": "object"`.

---

### CallToolResult

```cpp
using ContentItem = std::variant<TextContent, ImageContent, AudioContent,
                                 EmbeddedResource>;

struct CallToolResult {
    std::vector<ContentItem> content;
    std::optional<bool> is_error;          // true if handler signals a tool-level error
};
```

Returned from tool handler callbacks.

---

### TextContent

```cpp
struct TextContent {
    std::string text;
    std::optional<nlohmann::json> annotations;
};
```

---

### ImageContent

```cpp
struct ImageContent {
    std::string data;        // base64-encoded image bytes
    std::string mime_type;   // e.g. "image/png"
    std::optional<nlohmann::json> annotations;
};
```

---

### AudioContent

```cpp
struct AudioContent {
    std::string data;        // base64-encoded audio bytes
    std::string mime_type;   // e.g. "audio/wav"
    std::optional<nlohmann::json> annotations;
};
```

---

### ResourceDefinition

```cpp
struct ResourceDefinition {
    std::string uri;
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;
    std::optional<nlohmann::json> annotations;
};
```

Passed to `McpServer::add_resource()`.

---

### ResourceTemplate

```cpp
struct ResourceTemplate {
    std::string uri_template;   // RFC 6570 URI template
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;
    std::optional<nlohmann::json> annotations;
};
```

Passed to `McpServer::add_resource_template()`. The handler receives the expanded URI.

---

### ReadResourceResult

```cpp
using ResourceContents = std::variant<TextResourceContents, BlobResourceContents>;

struct ReadResourceResult {
    std::vector<ResourceContents> contents;
};
```

---

### TextResourceContents

```cpp
struct TextResourceContents {
    std::string uri;
    std::optional<std::string> mime_type;
    std::string text;
};
```

---

### BlobResourceContents

```cpp
struct BlobResourceContents {
    std::string uri;
    std::optional<std::string> mime_type;
    std::string blob;   // base64-encoded
};
```

---

### PromptDefinition

```cpp
struct PromptArgument {
    std::string name;
    std::optional<std::string> description;
    bool required = false;
};

struct PromptDefinition {
    std::string name;
    std::optional<std::string> description;
    std::vector<PromptArgument> arguments;
};
```

Passed to `McpServer::add_prompt()`.

---

### GetPromptResult

```cpp
enum class Role { User, Assistant };

struct PromptMessage {
    Role role;
    ContentItem content;
};

struct GetPromptResult {
    std::optional<std::string> description;
    std::vector<PromptMessage> messages;
};
```

---

### PromptArguments

```cpp
using PromptArguments = std::unordered_map<std::string, std::string>;
```

Map of argument name to value, passed to prompt handlers.

---

### LogLevel

```cpp
enum class LogLevel {
    Debug,
    Info,
    Notice,
    Warning,
    Error,
    Critical,
    Alert,
    Emergency
};
```

---

### McpError

```cpp
struct McpError {
    int code;               // JSON-RPC error code
    std::string message;
    std::optional<nlohmann::json> data;
};
```

Standard JSON-RPC codes:

| Constant                          | Code    | Meaning                        |
|-----------------------------------|---------|--------------------------------|
| `McpError::PARSE_ERROR`           | -32700  | Invalid JSON                   |
| `McpError::INVALID_REQUEST`       | -32600  | Not a valid JSON-RPC request   |
| `McpError::METHOD_NOT_FOUND`      | -32601  | Unknown method                 |
| `McpError::INVALID_PARAMS`        | -32602  | Bad parameters                 |
| `McpError::INTERNAL_ERROR`        | -32603  | Unexpected server error        |

---

## McpServer

```cpp
class McpServer {
public:
    struct Options {
        Implementation server_info;
        ServerCapabilities capabilities;   // auto-inferred from registrations if omitted
    };

    explicit McpServer(Options opts);

    // Tool registration
    void add_tool(ToolDefinition def,
                  std::function<CallToolResult(const nlohmann::json& args)> handler);

    // Resource registration (static URI)
    void add_resource(ResourceDefinition def,
                      std::function<ReadResourceResult()> handler);

    // Resource template registration (RFC 6570 URI template)
    void add_resource_template(
        ResourceTemplate def,
        std::function<ReadResourceResult(const std::string& uri)> handler);

    // Subscribe/unsubscribe callbacks (called when client subscribes/unsubscribes)
    void set_resource_subscribe_handler(
        std::function<void(const std::string& uri)> on_subscribe,
        std::function<void(const std::string& uri)> on_unsubscribe);

    // Prompt registration
    void add_prompt(PromptDefinition def,
                    std::function<GetPromptResult(const PromptArguments& args)> handler);

    // Completion provider
    void set_completion_handler(
        std::function<std::vector<std::string>(
            const std::string& ref_type,
            const std::string& ref_name,
            const std::string& argument_name,
            const std::string& argument_value)> handler);

    // Logging: send a log message to the connected client
    void send_log(LogLevel level,
                  const std::string& data,
                  std::optional<std::string> logger = std::nullopt);

    // Resource change notification (push to subscribed clients)
    void notify_resource_updated(const std::string& uri);
    void notify_resource_list_changed();
    void notify_tool_list_changed();
    void notify_resource_template_list_changed();
    void notify_prompt_list_changed();

    // Sampling (server -> client call)
    // Requires client to have advertised sampling capability.
    struct SamplingRequest {
        std::vector<PromptMessage> messages;
        std::optional<nlohmann::json> model_preferences;
        std::optional<std::string> system_prompt;
        std::optional<int> max_tokens;
    };
    struct SamplingResult {
        std::string model;
        std::string stop_reason;
        Role role;
        ContentItem content;
    };
    SamplingResult request_sampling(SamplingRequest req);

    // Elicitation (server -> client call)
    struct ElicitRequest {
        std::string message;
        nlohmann::json requested_schema;
    };
    struct ElicitResult {
        std::string action;   // "accept", "decline", or "cancel"
        std::optional<nlohmann::json> content;
    };
    ElicitResult request_elicitation(ElicitRequest req);

    // Transport: run until connection closes
    void serve_stdio();
    void serve_http(const std::string& host, uint16_t port);

    // Shutdown from a signal handler or another thread
    void shutdown();
};
```

### McpServer::add_tool

```cpp
server.add_tool(tool_def, [](const nlohmann::json& args) -> mcp::CallToolResult {
    // args is the "arguments" object from the tools/call request.
    // Throw mcp::McpException or return a result with is_error=true to signal errors.
    mcp::CallToolResult result;
    result.content.push_back(mcp::TextContent{"ok", std::nullopt});
    return result;
});
```

If the handler throws any exception, the session catches it and returns a JSON-RPC error
response with code `-32603`. Throw `mcp::McpException(code, message)` to control the
error code.

### McpServer::serve_stdio

Starts the stdio read loop. Blocks until stdin reaches EOF or `shutdown()` is called.
Must be called from the main thread (or the thread that owns the Session).

### McpServer::serve_http

Binds an HTTP listener and serves the Streamable HTTP transport. Blocks until the listener
is stopped or `shutdown()` is called.

---

## McpClient

```cpp
class McpClient {
public:
    struct Options {
        Implementation client_info;
        ClientCapabilities capabilities;
    };

    explicit McpClient(Options opts);

    // Connect and run the initialize handshake. Blocks until complete.
    void connect_stdio(const std::string& command,
                       const std::vector<std::string>& args = {},
                       const std::unordered_map<std::string, std::string>& env = {});

    void connect_http(const std::string& url);

    // Tools
    std::vector<ToolDefinition> list_tools(
        std::optional<std::string> cursor = std::nullopt);

    CallToolResult call_tool(const std::string& name,
                             const nlohmann::json& arguments = {});

    // Resources
    struct ListResourcesResult {
        std::vector<ResourceDefinition> resources;
        std::optional<std::string> next_cursor;
    };
    ListResourcesResult list_resources(
        std::optional<std::string> cursor = std::nullopt);

    ReadResourceResult read_resource(const std::string& uri);

    std::vector<ResourceTemplate> list_resource_templates(
        std::optional<std::string> cursor = std::nullopt);

    void subscribe_resource(const std::string& uri);
    void unsubscribe_resource(const std::string& uri);

    // Set callback invoked when a resources/updated notification arrives
    void on_resource_updated(std::function<void(const std::string& uri)> cb);

    // Prompts
    struct ListPromptsResult {
        std::vector<PromptDefinition> prompts;
        std::optional<std::string> next_cursor;
    };
    ListPromptsResult list_prompts(
        std::optional<std::string> cursor = std::nullopt);

    GetPromptResult get_prompt(const std::string& name,
                               const PromptArguments& args = {});

    // Completions
    std::vector<std::string> complete(const std::string& ref_type,
                                      const std::string& ref_name,
                                      const std::string& argument_name,
                                      const std::string& argument_value);

    // Logging: set minimum level the server should send
    void set_log_level(LogLevel level);
    void on_log_message(std::function<void(LogLevel, const std::string& logger,
                                           const nlohmann::json& data)> cb);

    // Roots (list of filesystem roots the client exposes to the server)
    void set_roots(const std::vector<std::string>& uris);

    // Sampling handler (server -> client)
    void set_sampling_handler(
        std::function<McpServer::SamplingResult(
            const McpServer::SamplingRequest&)> handler);

    // Elicitation handler (server -> client)
    void set_elicitation_handler(
        std::function<McpServer::ElicitResult(
            const McpServer::ElicitRequest&)> handler);

    // Disconnect and clean up
    void disconnect();

    // Returns the ServerCapabilities negotiated during initialize
    ServerCapabilities server_capabilities() const;
    Implementation server_info() const;
};
```

### McpClient::connect_stdio

Spawns `command` as a subprocess with the given arguments and environment, connects its
stdin/stdout as the MCP stdio transport, and performs the `initialize` handshake.

```cpp
mcp::McpClient client{{{"my-client", std::nullopt, "1.0"}}};
client.connect_stdio("./build/examples/echo_server");

auto tools = client.list_tools();
for (const auto& t : tools) {
    std::cout << t.name << ": " << t.description.value_or("") << "\n";
}

auto result = client.call_tool("echo", {{"message", "hello"}});
```

### McpClient::call_tool

Sends a `tools/call` request and blocks until the response arrives or the timeout
expires. Throws `McpException` if the server returns an error response.

---

## Error Handling

### McpException

```cpp
class McpException : public std::runtime_error {
public:
    McpException(int code, std::string message,
                 std::optional<nlohmann::json> data = std::nullopt);

    int code() const noexcept;
    const std::optional<nlohmann::json>& data() const noexcept;
};
```

Thrown by client methods when the server returns a JSON-RPC error response. Also thrown
by server internals for protocol violations.

Throw `McpException` from a tool/resource/prompt handler to send a specific error code
back to the client:

```cpp
server.add_tool(def, [](const nlohmann::json& args) -> mcp::CallToolResult {
    if (!args.contains("name"))
        throw mcp::McpException(mcp::McpError::INVALID_PARAMS, "missing 'name'");
    // ...
});
```

### Tool-Level Errors

For errors that are part of normal tool operation (not protocol errors), set
`is_error = true` on the result instead of throwing:

```cpp
mcp::CallToolResult result;
result.is_error = true;
result.content.push_back(mcp::TextContent{"File not found: /tmp/missing.txt", std::nullopt});
return result;
```

This distinction matters: a `McpException` causes the JSON-RPC `error` field to be set,
while `is_error = true` results in a successful JSON-RPC response whose `result.isError`
field is set, allowing the LLM to see and recover from the error message.

---

## Transport Factories

You can provide a custom transport by implementing the `Transport` interface:

```cpp
class Transport {
public:
    virtual ~Transport() = default;

    // Read one complete message frame. Returns false on EOF.
    virtual bool read_message(std::string& out) = 0;

    // Write one complete message frame.
    virtual void write_message(const std::string& msg) = 0;

    // Called once when the session is starting.
    virtual void start() {}

    // Called once when the session is shutting down.
    virtual void stop() {}
};
```

Pass a `std::unique_ptr<Transport>` to the Session constructor directly when using the
lower-level Session API.

---

## Thread Safety

- `McpServer` and `McpClient` are NOT thread-safe. All method calls must come from the
  thread running the event loop (i.e., the thread that called `serve_stdio()` or
  `connect_stdio()`), EXCEPT:
  - `McpServer::shutdown()` and `McpClient::disconnect()` are safe to call from any
    thread.
  - `McpServer::send_log()` and `McpServer::notify_*()` are safe to call from any thread
    when `MCPXX_THREAD_POOL` is enabled (they post through a thread-safe queue).

---

## Version

The library version is accessible at compile time:

```cpp
#include <mcp/version.hpp>

static_assert(MCPXX_VERSION_MAJOR == 0);
static_assert(MCPXX_VERSION_MINOR == 1);
static_assert(MCPXX_VERSION_PATCH == 0);

// Or as a string:
// MCPXX_VERSION_STRING == "0.1.0"
```
