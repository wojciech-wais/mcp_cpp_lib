/// Client example — demonstrates connecting to an MCP server and using its capabilities.
/// Usage: ./client_example <server_command> [args...]
/// Example: ./client_example ./echo_server

#include <mcp/mcp.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_command> [args...]\n";
        std::cerr << "Example: " << argv[0] << " ./echo_server\n";
        return 1;
    }

    // Build server command and arguments
    std::string command = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // Create client
    mcp::McpClient::Options opts;
    opts.client_info = {"example-client", std::nullopt, "1.0.0"};
    opts.request_timeout = std::chrono::milliseconds(10000);
    mcp::McpClient client{std::move(opts)};

    // Register notification callbacks
    client.on_tools_changed([]() {
        std::cout << "[notification] Server's tool list changed.\n";
    });
    client.on_log_message([](const mcp::LogMessage& msg) {
        std::cout << "[log " << mcp::log_level_to_string(msg.level) << "] "
                  << msg.data.dump() << "\n";
    });
    client.on_progress([](const mcp::ProgressInfo& info) {
        std::cout << "[progress] " << info.progress;
        if (info.total) std::cout << " / " << *info.total;
        if (info.message) std::cout << " - " << *info.message;
        std::cout << "\n";
    });

    try {
        // Connect and initialize
        std::cout << "Connecting to: " << command << "\n";
        client.connect_stdio(command, args);
        auto init = client.initialize();
        std::cout << "Connected to: " << init.server_info.name
                  << " v" << init.server_info.version
                  << " (protocol " << init.protocol_version << ")\n";
        if (init.instructions) {
            std::cout << "Instructions: " << *init.instructions << "\n";
        }

        // List tools
        std::cout << "\n--- Tools ---\n";
        auto tools = client.list_tools();
        for (const auto& tool : tools.items) {
            std::cout << "  " << tool.name;
            if (tool.description) std::cout << " - " << *tool.description;
            std::cout << "\n";
        }

        // Call the echo tool if available
        for (const auto& tool : tools.items) {
            if (tool.name == "echo") {
                std::cout << "\n--- Calling echo tool ---\n";
                auto result = client.call_tool("echo", {{"text", "Hello from C++ client!"}});
                if (result.is_error) {
                    std::cout << "  Error!\n";
                } else {
                    for (const auto& content : result.content) {
                        if (auto* tc = std::get_if<mcp::TextContent>(&content)) {
                            std::cout << "  Response: " << tc->text << "\n";
                        }
                    }
                }
                break;
            }
        }

        // List resources
        std::cout << "\n--- Resources ---\n";
        auto resources = client.list_resources();
        if (resources.items.empty()) {
            std::cout << "  (none)\n";
        }
        for (const auto& res : resources.items) {
            std::cout << "  " << res.uri << " (" << res.name << ")\n";
        }

        // List prompts
        std::cout << "\n--- Prompts ---\n";
        auto prompts = client.list_prompts();
        if (prompts.items.empty()) {
            std::cout << "  (none)\n";
        }
        for (const auto& p : prompts.items) {
            std::cout << "  " << p.name;
            if (p.description) std::cout << " - " << *p.description;
            std::cout << "\n";
        }

        // Ping
        std::cout << "\nPing... ";
        client.ping();
        std::cout << "OK\n";

        // Disconnect
        client.disconnect();
        std::cout << "Disconnected.\n";

    } catch (const mcp::McpError& e) {
        std::cerr << "MCP error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
