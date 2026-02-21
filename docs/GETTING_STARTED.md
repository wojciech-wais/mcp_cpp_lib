# Getting Started with mcpxx

This guide walks you through building mcpxx from source, running the bundled echo server,
writing your first MCP server in C++, and connecting it to Claude Desktop.

---

## Prerequisites

| Tool      | Minimum version | Notes                                      |
|-----------|-----------------|--------------------------------------------|
| GCC       | 13              | Or use Clang 17+. Both are tested in CI.   |
| Clang     | 17              | Alternative to GCC.                        |
| CMake     | 3.22            | Required for FetchContent and presets.     |
| Ninja     | any recent      | Recommended generator; Make also works.    |
| git       | any recent      | For cloning and FetchContent.              |
| Python    | 3.10+           | Only if you enable `MCPXX_BUILD_PYTHON`.   |

### Installing prerequisites on Ubuntu 22.04

```bash
sudo apt-get update
sudo apt-get install -y \
    cmake ninja-build git \
    gcc-13 g++-13
# Or for Clang:
# sudo apt-get install -y clang-17
```

### Installing prerequisites on macOS (Homebrew)

```bash
brew install cmake ninja llvm@17
export CC=$(brew --prefix llvm@17)/bin/clang
export CXX=$(brew --prefix llvm@17)/bin/clang++
```

---

## Build Steps

### 1. Clone the repository

```bash
git clone https://github.com/yourorg/mcpxx
cd mcpxx
```

### 2. Configure with CMake

```bash
cmake -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++-13 \
    -DMCPXX_BUILD_TESTS=ON \
    -DMCPXX_BUILD_EXAMPLES=ON
```

CMake will automatically download all dependencies (nlohmann/json, asio, etc.) via
FetchContent on first configure. An internet connection is required.

### 3. Build

```bash
cmake --build build -j$(nproc)
```

### 4. Run the test suite

```bash
ctest --test-dir build --output-on-failure
```

All tests should pass. If any fail, please open an issue with the full output.

---

## Running the Echo Server Example

The echo server is the simplest possible MCP server. It registers one tool, `echo`, that
returns its `message` argument unchanged.

```bash
./build/examples/echo_server
```

The server speaks the MCP stdio transport: it reads JSON-RPC frames from stdin and writes
responses to stdout. You can interact with it manually:

```bash
# Send initialize
printf '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"0.1"}}}\n' \
    | ./build/examples/echo_server
```

---

## Creating Your First Server

The following example creates an MCP server with a single `greet` tool.

### Source file: `my_server.cpp`

```cpp
#include <mcp/mcp.hpp>
#include <iostream>

int main() {
    // Configure server identity
    mcp::McpServer::Options opts;
    opts.server_info.name    = "greet-server";
    opts.server_info.version = "1.0.0";

    mcp::McpServer server{std::move(opts)};

    // Define the tool schema
    mcp::ToolDefinition greet_tool;
    greet_tool.name        = "greet";
    greet_tool.description = "Return a greeting for the given name.";
    greet_tool.input_schema = {
        {"type", "object"},
        {"properties", {
            {"name", {
                {"type", "string"},
                {"description", "Name of the person to greet"}
            }}
        }},
        {"required", {"name"}}
    };

    // Register the handler
    server.add_tool(greet_tool, [](const nlohmann::json& args) -> mcp::CallToolResult {
        const std::string name = args.at("name").get<std::string>();

        mcp::CallToolResult result;
        result.content.push_back(
            mcp::TextContent{"Hello, " + name + "! Welcome to mcpxx.", std::nullopt}
        );
        return result;
    });

    // Start serving on stdio (blocks until stdin closes)
    server.serve_stdio();
    return 0;
}
```

### CMakeLists.txt for the server

```cmake
cmake_minimum_required(VERSION 3.22)
project(my_greet_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(mcpxx REQUIRED)

add_executable(greet_server my_server.cpp)
target_link_libraries(greet_server PRIVATE mcpxx::mcpxx)
```

### Build

```bash
# Install mcpxx first (or use FetchContent to pull it in)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/greet_server
```

---

## Registering a Resource

Resources expose data that the LLM can read. The example below exposes a single text
resource containing a configuration file.

```cpp
mcp::ResourceDefinition config_resource;
config_resource.uri         = "file:///etc/myapp/config.toml";
config_resource.name        = "App configuration";
config_resource.description = "Current application configuration";
config_resource.mime_type   = "text/plain";

server.add_resource(config_resource, []() -> mcp::ReadResourceResult {
    mcp::ReadResourceResult result;
    result.contents.push_back(mcp::TextResourceContents{
        "file:///etc/myapp/config.toml",
        "text/plain",
        "[server]\nport = 8080\n"
    });
    return result;
});
```

---

## Registering a Prompt

Prompts are reusable message templates.

```cpp
mcp::PromptDefinition review_prompt;
review_prompt.name        = "code_review";
review_prompt.description = "Ask the model to review a code snippet";
review_prompt.arguments   = {
    {"code",     "The code to review",  true},
    {"language", "Programming language", false}
};

server.add_prompt(review_prompt, [](const mcp::PromptArguments& args)
        -> mcp::GetPromptResult {
    const std::string code = args.at("code");
    const std::string lang = args.count("language") ? args.at("language") : "unknown";

    mcp::GetPromptResult result;
    result.description = "Code review prompt";
    result.messages.push_back({
        mcp::Role::User,
        mcp::TextContent{"Please review this " + lang + " code:\n\n```\n" + code + "\n```",
                         std::nullopt}
    });
    return result;
});
```

---

## Connecting with Claude Desktop

Claude Desktop can launch your server as a subprocess over stdio. Add an entry to its
configuration file.

### Configuration file location

- **macOS**: `~/Library/Application Support/Claude/claude_desktop_config.json`
- **Linux**: `~/.config/Claude/claude_desktop_config.json`
- **Windows**: `%APPDATA%\Claude\claude_desktop_config.json`

### Configuration entry

```json
{
  "mcpServers": {
    "greet-server": {
      "command": "/absolute/path/to/build/greet_server",
      "args": [],
      "env": {}
    }
  }
}
```

Replace `/absolute/path/to/build/greet_server` with the actual path to your compiled
binary. Restart Claude Desktop after saving the file. The `greet` tool will then appear
in Claude's tool picker.

---

## Next Steps

- Read [API_REFERENCE.md](API_REFERENCE.md) for the complete public API.
- Read [ARCHITECTURE.md](ARCHITECTURE.md) to understand how the library is structured.
- Browse the `examples/` directory for more complete examples including resource
  subscriptions, progress reporting, and the HTTP transport.
