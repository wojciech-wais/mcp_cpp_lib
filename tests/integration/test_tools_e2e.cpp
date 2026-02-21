#include <gtest/gtest.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include <unistd.h>
#include <thread>
#include <chrono>

using namespace mcp;

class ToolsE2ETest : public ::testing::Test {
protected:
    int c2s_[2], s2c_[2];
    std::unique_ptr<McpServer> server_;
    std::unique_ptr<McpClient> client_;
    std::thread server_thread_;

    void SetUp() override {
        ASSERT_EQ(pipe(c2s_), 0);
        ASSERT_EQ(pipe(s2c_), 0);

        McpServer::Options sopts;
        sopts.server_info = {"test-server", std::nullopt, "1.0"};
        sopts.thread_pool_size = 2;
        server_ = std::make_unique<McpServer>(sopts);

        // Register echo tool
        ToolDefinition echo_def;
        echo_def.name = "echo";
        echo_def.description = "Echo the input text";
        echo_def.input_schema = {
            {"type", "object"},
            {"properties", {{"text", {{"type", "string"}}}}},
            {"required", {"text"}}
        };
        server_->add_tool(echo_def, [](const nlohmann::json& args) -> CallToolResult {
            std::string text = args.at("text").get<std::string>();
            CallToolResult result;
            result.content.push_back(TextContent{text, std::nullopt});
            return result;
        });

        // Register error tool
        ToolDefinition error_def;
        error_def.name = "fail";
        error_def.input_schema = nlohmann::json{{"type", "object"}};
        server_->add_tool(error_def, [](const nlohmann::json&) -> CallToolResult {
            throw std::runtime_error("Tool intentionally failed");
            return {};
        });

        auto server_transport = std::make_unique<StdioTransport>(c2s_[0], s2c_[1]);
        server_thread_ = std::thread([this, t = std::move(server_transport)]() mutable {
            server_->serve(std::move(t));
        });

        McpClient::Options copts;
        copts.client_info = {"test-client", std::nullopt, "1.0"};
        copts.request_timeout = std::chrono::milliseconds(5000);
        client_ = std::make_unique<McpClient>(copts);

        auto client_transport = std::make_unique<StdioTransport>(s2c_[0], c2s_[1]);
        client_->connect(std::move(client_transport));
        client_->initialize();
    }

    void TearDown() override {
        client_->disconnect();
        server_->shutdown();
        if (server_thread_.joinable()) server_thread_.join();
        close(c2s_[0]); close(c2s_[1]);
        close(s2c_[0]); close(s2c_[1]);
    }
};

TEST_F(ToolsE2ETest, ListTools) {
    auto result = client_->list_tools();
    ASSERT_EQ(result.items.size(), 2u);
    // Check echo tool
    bool found_echo = false;
    for (const auto& tool : result.items) {
        if (tool.name == "echo") { found_echo = true; break; }
    }
    EXPECT_TRUE(found_echo);
}

TEST_F(ToolsE2ETest, CallEchoTool) {
    auto result = client_->call_tool("echo", {{"text", "Hello, MCP!"}});
    EXPECT_FALSE(result.is_error);
    ASSERT_EQ(result.content.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TextContent>(result.content[0]));
    EXPECT_EQ(std::get<TextContent>(result.content[0]).text, "Hello, MCP!");
}

TEST_F(ToolsE2ETest, CallUnknownTool) {
    EXPECT_THROW(client_->call_tool("nonexistent_tool", {}), McpProtocolError);
}

TEST_F(ToolsE2ETest, CallToolThatThrows) {
    // Tool that throws should return is_error=true, not throw on client
    auto result = client_->call_tool("fail", {});
    EXPECT_TRUE(result.is_error);
}

TEST_F(ToolsE2ETest, Ping) {
    EXPECT_NO_THROW(client_->ping());
}

TEST_F(ToolsE2ETest, ListToolsPaginated) {
    // Add many tools
    for (int i = 0; i < 60; ++i) {
        ToolDefinition def;
        def.name = "tool_" + std::to_string(i);
        def.input_schema = nlohmann::json{{"type", "object"}};
        server_->add_tool(def, [](const nlohmann::json&) -> CallToolResult { return {}; });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // List first page
    auto page1 = client_->list_tools();
    EXPECT_LE(page1.items.size(), 50u);

    if (page1.next_cursor) {
        // List second page
        auto page2 = client_->list_tools(*page1.next_cursor);
        EXPECT_FALSE(page2.items.empty());
    }
}
