/// Echo server — minimal MCP server demonstrating tool registration.
/// Usage: ./echo_server
/// Communicates over stdio (newline-delimited JSON-RPC).

#include <mcp/mcp.hpp>
#include <iostream>

int main() {
    mcp::McpServer::Options opts;
    opts.server_info = {"echo-server", std::nullopt, "1.0.0"};
    opts.instructions = "A simple echo server that returns whatever you send it.";

    mcp::McpServer server{std::move(opts)};

    // Register the echo tool
    mcp::ToolDefinition echo_tool;
    echo_tool.name = "echo";
    echo_tool.description = "Echo the input text back to the caller";
    echo_tool.input_schema = {
        {"type", "object"},
        {"properties", {
            {"text", {{"type", "string"}, {"description", "The text to echo"}}}
        }},
        {"required", {"text"}}
    };

    server.add_tool(echo_tool, [](const nlohmann::json& args) -> mcp::CallToolResult {
        std::string text = args.at("text").get<std::string>();
        mcp::CallToolResult result;
        result.content.push_back(mcp::TextContent{text, std::nullopt});
        return result;
    });

    // Serve over stdio — blocks until shutdown
    server.serve_stdio();
    return 0;
}
