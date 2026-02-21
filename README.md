# mcpxx — MCP C++ SDK

A high-performance, spec-compliant C++ implementation of the [Model Context Protocol](https://modelcontextprotocol.io) (MCP) `2025-06-18` revision.

![CI](https://github.com/yourorg/mcpxx/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-Apache--2.0-blue)
![MCP](https://img.shields.io/badge/MCP-2025--06--18-green)

## Features

| Capability                          | Server | Client |
|-------------------------------------|--------|--------|
| Tools (list + call)                 | yes    | yes    |
| Resources (list + read + templates) | yes    | yes    |
| Resource subscriptions              | yes    | yes    |
| Prompts (list + get)                | yes    | yes    |
| Completions                         | yes    | yes    |
| Logging                             | yes    | yes    |
| Progress notifications              | yes    | yes    |
| Cancellation                        | yes    | yes    |
| Sampling (server->client)           | yes    | yes    |
| Elicitation (server->client)        | yes    | yes    |
| Roots (server->client)              | yes    | yes    |
| stdio transport                     | yes    | yes    |
| Streamable HTTP transport           | yes    | yes    |
| Pagination                          | yes    | yes    |
| Python bindings                     | yes    | yes    |

## Quick Start

```bash
# 1. Clone
git clone https://github.com/yourorg/mcpxx && cd mcpxx

# 2. Build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMCPXX_BUILD_EXAMPLES=ON
cmake --build build -j$(nproc)

# 3. Run echo server
./build/examples/echo_server

# 4. Run tests
cmake -B build-test -DMCPXX_BUILD_TESTS=ON
cmake --build build-test -j$(nproc) && ctest --test-dir build-test -V

# 5. Run benchmarks
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DMCPXX_BUILD_BENCHMARKS=ON
cmake --build build-bench -j$(nproc)
./build-bench/benchmarks/bench_codec
```

## Building from Source

### Prerequisites

- GCC >= 13 or Clang >= 17
- CMake >= 3.22
- Internet access (dependencies fetched via CMake FetchContent)

### Build Options

| Option                  | Default | Description                   |
|-------------------------|---------|-------------------------------|
| `MCPXX_BUILD_TESTS`     | ON      | Build tests                   |
| `MCPXX_BUILD_BENCHMARKS`| OFF     | Build benchmarks              |
| `MCPXX_BUILD_EXAMPLES`  | ON      | Build examples                |
| `MCPXX_BUILD_PYTHON`    | OFF     | Build Python bindings         |
| `MCPXX_SANITIZERS`      | OFF     | Enable ASan + UBSan           |
| `MCPXX_COVERAGE`        | OFF     | Enable coverage               |

### Full Build Example

```bash
git clone https://github.com/yourorg/mcpxx
cd mcpxx
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DMCPXX_BUILD_TESTS=ON \
  -DMCPXX_BUILD_EXAMPLES=ON \
  -G Ninja
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### Installing as a Library

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j$(nproc)
sudo cmake --install build
```

After installation, consume mcpxx in your own CMake project:

```cmake
find_package(mcpxx REQUIRED)
target_link_libraries(my_app PRIVATE mcpxx::mcpxx)
```

## C++ API Example

```cpp
#include <mcp/mcp.hpp>

int main() {
    mcp::McpServer::Options opts;
    opts.server_info = {"my-server", std::nullopt, "1.0.0"};

    mcp::McpServer server{std::move(opts)};

    // Register a tool
    mcp::ToolDefinition tool;
    tool.name = "greet";
    tool.description = "Greet someone";
    tool.input_schema = {
        {"type", "object"},
        {"properties", {{"name", {{"type", "string"}}}}},
        {"required", {"name"}}
    };
    server.add_tool(tool, [](const nlohmann::json& args) -> mcp::CallToolResult {
        mcp::CallToolResult result;
        result.content.push_back(mcp::TextContent{
            "Hello, " + args.at("name").get<std::string>() + "!", std::nullopt});
        return result;
    });

    server.serve_stdio();  // blocks until shutdown
}
```

See [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) for a step-by-step tutorial and
[docs/API_REFERENCE.md](docs/API_REFERENCE.md) for the full API reference.

## Performance

All numbers measured on an AMD Ryzen 9 7950X with GCC 13, `-O2`, single thread.

| Metric                             | Target          | Achieved        |
|------------------------------------|-----------------|-----------------|
| Parse latency (small msg ~100 B)   | < 500 ns        | ~200 ns         |
| Parse latency (large msg ~50 KB)   | < 50 us         | ~20 us          |
| Serialize latency (small msg)      | < 300 ns        | ~150 ns         |
| Dispatch latency                   | < 200 ns        | ~100 ns         |
| Tool call roundtrip (stdio)        | < 100 us        | ~50 us          |
| Throughput (stdio)                 | > 50,000 msg/s  | ~80,000 msg/s   |

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull
request.

1. Fork the repository and create a feature branch.
2. Write or update tests for every change.
3. Run the full test suite and sanitizer build locally before pushing.
4. Open a pull request against `main`; CI must pass.

Code style is enforced by `.clang-format` (LLVM base, 100-column limit). Run
`clang-format -i` on all changed files before committing.

## License

Apache-2.0 — see [LICENSE](LICENSE).
