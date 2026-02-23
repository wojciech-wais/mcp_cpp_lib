#include <gtest/gtest.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/transport/http_transport.hpp"
#include <thread>
#include <chrono>

using namespace mcp;

class HttpE2ETest : public ::testing::Test {
protected:
    std::unique_ptr<McpServer> server_;
    std::unique_ptr<McpClient> client_;
    std::thread server_thread_;
    static constexpr uint16_t port_ = 18923;

    void SetUp() override {
        McpServer::Options sopts;
        sopts.server_info = {"http-test-server", std::nullopt, "1.0"};
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

        // Register a resource
        ResourceDefinition res_def;
        res_def.uri = "test://greeting";
        res_def.name = "Greeting";
        res_def.mime_type = "text/plain";
        server_->add_resource(res_def, [](const std::string&) -> std::vector<ResourceContent> {
            return {ResourceContent{"test://greeting", "text/plain", std::string("Hello from HTTP!"), std::nullopt}};
        });

        // Start server on HTTP
        server_thread_ = std::thread([this]() {
            server_->serve_http("127.0.0.1", port_);
        });

        // Wait for server to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Connect client via HTTP
        McpClient::Options copts;
        copts.client_info = {"http-test-client", std::nullopt, "1.0"};
        copts.request_timeout = std::chrono::milliseconds(5000);
        client_ = std::make_unique<McpClient>(copts);
        client_->connect_http("http://127.0.0.1:" + std::to_string(port_) + "/mcp");
    }

    void TearDown() override {
        client_->disconnect();
        server_->shutdown();
        if (server_thread_.joinable()) server_thread_.join();
    }
};

TEST_F(HttpE2ETest, InitializeAndPing) {
    auto result = client_->initialize();
    EXPECT_EQ(result.server_info.name, "http-test-server");
    EXPECT_FALSE(result.protocol_version.empty());
    EXPECT_NO_THROW(client_->ping());
}

TEST_F(HttpE2ETest, ListTools) {
    auto init = client_->initialize();
    auto tools = client_->list_tools();
    ASSERT_EQ(tools.items.size(), 1u);
    EXPECT_EQ(tools.items[0].name, "echo");
}

TEST_F(HttpE2ETest, CallTool) {
    auto init = client_->initialize();
    auto result = client_->call_tool("echo", {{"text", "HTTP round-trip"}});
    EXPECT_FALSE(result.is_error);
    ASSERT_EQ(result.content.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TextContent>(result.content[0]));
    EXPECT_EQ(std::get<TextContent>(result.content[0]).text, "HTTP round-trip");
}

TEST_F(HttpE2ETest, ReadResource) {
    auto init = client_->initialize();
    auto contents = client_->read_resource("test://greeting");
    ASSERT_EQ(contents.size(), 1u);
    EXPECT_EQ(contents[0].text.value_or(""), "Hello from HTTP!");
}

TEST_F(HttpE2ETest, CallUnknownTool) {
    auto init = client_->initialize();
    EXPECT_THROW(client_->call_tool("nonexistent", {}), McpProtocolError);
}
