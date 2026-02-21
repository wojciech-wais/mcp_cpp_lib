#include <gtest/gtest.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include <unistd.h>
#include <thread>

using namespace mcp;

class PromptsE2ETest : public ::testing::Test {
protected:
    int c2s_[2], s2c_[2];
    std::unique_ptr<McpServer> server_;
    std::unique_ptr<McpClient> client_;
    std::thread server_thread_;

    void SetUp() override {
        ASSERT_EQ(pipe(c2s_), 0);
        ASSERT_EQ(pipe(s2c_), 0);

        McpServer::Options sopts;
        sopts.server_info = {"prompt-server", std::nullopt, "1.0"};
        server_ = std::make_unique<McpServer>(sopts);

        PromptDefinition pd;
        pd.name = "code_review";
        pd.description = "Review code";
        pd.arguments = {{"code", std::string("Code to review"), true}};
        server_->add_prompt(pd, [](const std::string&, const nlohmann::json& args) -> GetPromptResult {
            std::string code = args.value("code", "");
            GetPromptResult result;
            result.messages.push_back({"user", TextContent{"Please review: " + code, std::nullopt}});
            return result;
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

TEST_F(PromptsE2ETest, ListPrompts) {
    auto result = client_->list_prompts();
    ASSERT_EQ(result.items.size(), 1u);
    EXPECT_EQ(result.items[0].name, "code_review");
}

TEST_F(PromptsE2ETest, GetPrompt) {
    auto result = client_->get_prompt("code_review", {{"code", "int main() {}"}});
    ASSERT_EQ(result.messages.size(), 1u);
    EXPECT_EQ(result.messages[0].role, "user");
    ASSERT_TRUE(std::holds_alternative<TextContent>(result.messages[0].content));
    auto text = std::get<TextContent>(result.messages[0].content).text;
    EXPECT_NE(text.find("int main()"), std::string::npos);
}

TEST_F(PromptsE2ETest, GetUnknownPrompt) {
    EXPECT_THROW(client_->get_prompt("nonexistent", {}), McpProtocolError);
}
