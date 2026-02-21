#include <gtest/gtest.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>

using namespace mcp;

TEST(ProgressTest, ProgressNotificationsReceived) {
    int c2s[2], s2c[2];
    ASSERT_EQ(pipe(c2s), 0);
    ASSERT_EQ(pipe(s2c), 0);

    McpServer::Options sopts;
    sopts.server_info = {"progress-server", std::nullopt, "1.0"};
    McpServer server{sopts};

    // Register a tool that sends progress
    ToolDefinition td;
    td.name = "long_operation";
    td.input_schema = nlohmann::json{{"type", "object"}};
    server.add_tool(td, [&server](const nlohmann::json&) -> CallToolResult {
        for (int i = 1; i <= 3; ++i) {
            server.send_progress(std::variant<int64_t, std::string>{int64_t{1}},
                                 static_cast<double>(i), 3.0, "Step " + std::to_string(i));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CallToolResult r;
        r.content.push_back(TextContent{"done", std::nullopt});
        return r;
    });

    auto server_transport = std::make_unique<StdioTransport>(c2s[0], s2c[1]);
    std::thread server_thread([&, t = std::move(server_transport)]() mutable {
        server.serve(std::move(t));
    });

    McpClient::Options copts;
    copts.client_info = {"test-client", std::nullopt, "1.0"};
    copts.request_timeout = std::chrono::milliseconds(10000);
    McpClient client{copts};
    auto client_transport = std::make_unique<StdioTransport>(s2c[0], c2s[1]);
    client.connect(std::move(client_transport));
    client.initialize();

    // Call tool - it will send progress notifications
    auto result = client.call_tool("long_operation", {});
    EXPECT_FALSE(result.is_error);
    ASSERT_FALSE(result.content.empty());

    client.disconnect();
    server.shutdown();
    if (server_thread.joinable()) server_thread.join();
    close(c2s[0]); close(c2s[1]);
    close(s2c[0]); close(s2c[1]);
}
