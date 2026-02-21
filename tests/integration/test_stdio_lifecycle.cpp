#include <gtest/gtest.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include <unistd.h>
#include <thread>
#include <chrono>

using namespace mcp;

TEST(StdioLifecycle, FullLifecycle) {
    int c2s[2], s2c[2];
    ASSERT_EQ(pipe(c2s), 0);
    ASSERT_EQ(pipe(s2c), 0);

    McpServer::Options sopts;
    sopts.server_info = {"lifecycle-server", std::nullopt, "1.0"};
    McpServer server{sopts};

    auto server_transport = std::make_unique<StdioTransport>(c2s[0], s2c[1]);
    std::thread server_thread([&, t = std::move(server_transport)]() mutable {
        server.serve(std::move(t));
    });

    McpClient::Options copts;
    copts.client_info = {"lifecycle-client", std::nullopt, "1.0"};
    copts.request_timeout = std::chrono::milliseconds(5000);
    McpClient client{copts};

    auto client_transport = std::make_unique<StdioTransport>(s2c[0], c2s[1]);
    client.connect(std::move(client_transport));

    // Initialize
    auto init_result = client.initialize();
    EXPECT_EQ(init_result.server_info.name, "lifecycle-server");
    EXPECT_EQ(init_result.protocol_version, "2025-06-18");
    EXPECT_TRUE(client.is_connected());

    // Ping
    EXPECT_NO_THROW(client.ping());

    // Disconnect
    client.disconnect();
    server.shutdown();

    if (server_thread.joinable()) server_thread.join();

    close(c2s[0]); close(c2s[1]);
    close(s2c[0]); close(s2c[1]);
}

TEST(StdioLifecycle, InitializeResultHasCapabilities) {
    int c2s[2], s2c[2];
    ASSERT_EQ(pipe(c2s), 0);
    ASSERT_EQ(pipe(s2c), 0);

    McpServer::Options sopts;
    sopts.server_info = {"caps-server", std::nullopt, "2.0"};
    McpServer server{sopts};

    ToolDefinition td;
    td.name = "test_tool";
    td.input_schema = nlohmann::json{{"type", "object"}};
    server.add_tool(td, [](const nlohmann::json&) -> CallToolResult { return {}; });

    auto server_transport = std::make_unique<StdioTransport>(c2s[0], s2c[1]);
    std::thread server_thread([&, t = std::move(server_transport)]() mutable {
        server.serve(std::move(t));
    });

    McpClient::Options copts;
    copts.client_info = {"test-client", std::nullopt, "1.0"};
    copts.request_timeout = std::chrono::milliseconds(5000);
    McpClient client{copts};

    auto client_transport = std::make_unique<StdioTransport>(s2c[0], c2s[1]);
    client.connect(std::move(client_transport));

    auto result = client.initialize();
    EXPECT_TRUE(result.capabilities.tools.has_value());

    client.disconnect();
    server.shutdown();
    if (server_thread.joinable()) server_thread.join();
    close(c2s[0]); close(c2s[1]);
    close(s2c[0]); close(s2c[1]);
}
