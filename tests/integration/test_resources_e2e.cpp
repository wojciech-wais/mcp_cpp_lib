#include <gtest/gtest.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include <unistd.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace mcp;

class ResourcesE2ETest : public ::testing::Test {
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
        server_ = std::make_unique<McpServer>(sopts);

        ResourceDefinition rd;
        rd.uri = "file:///config.json";
        rd.name = "Config";
        rd.mime_type = "application/json";
        server_->add_resource(rd, [](const std::string& uri) -> std::vector<ResourceContent> {
            return {ResourceContent{uri, std::string("application/json"),
                    std::string("{\"key\":\"value\"}"), std::nullopt}};
        });

        // Add template
        ResourceTemplate tmpl;
        tmpl.uri_template = "file:///{path}";
        tmpl.name = "File";
        server_->add_resource_template(tmpl, [](const std::string& uri) -> std::vector<ResourceContent> {
            return {ResourceContent{uri, std::nullopt, std::string("file content"), std::nullopt}};
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

TEST_F(ResourcesE2ETest, ListResources) {
    auto result = client_->list_resources();
    ASSERT_EQ(result.items.size(), 1u);
    EXPECT_EQ(result.items[0].uri, "file:///config.json");
}

TEST_F(ResourcesE2ETest, ReadResource) {
    auto contents = client_->read_resource("file:///config.json");
    ASSERT_EQ(contents.size(), 1u);
    ASSERT_TRUE(contents[0].text.has_value());
    EXPECT_EQ(*contents[0].text, "{\"key\":\"value\"}");
}

TEST_F(ResourcesE2ETest, ReadNonexistentResource) {
    // Use a URI scheme that doesn't match any registered resource or template
    EXPECT_THROW(client_->read_resource("custom://nonexistent"), McpProtocolError);
}

TEST_F(ResourcesE2ETest, ListResourceTemplates) {
    auto result = client_->list_resource_templates();
    ASSERT_EQ(result.items.size(), 1u);
    EXPECT_EQ(result.items[0].uri_template, "file:///{path}");
}

TEST_F(ResourcesE2ETest, SubscribeAndNotify) {
    std::atomic<bool> notified{false};
    std::string notified_uri;

    client_->on_resource_updated([&](const std::string& uri) {
        notified_uri = uri;
        notified = true;
    });

    client_->subscribe_resource("file:///config.json");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    server_->notify_resource_updated("file:///config.json");

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!notified && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(notified);
    EXPECT_EQ(notified_uri, "file:///config.json");

    client_->unsubscribe_resource("file:///config.json");
}
