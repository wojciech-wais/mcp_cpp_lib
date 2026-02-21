#include <gtest/gtest.h>
#include "mcp/client.hpp"

using namespace mcp;

McpClient make_test_client() {
    McpClient::Options opts;
    opts.client_info = {"test-client", std::nullopt, "1.0.0"};
    return McpClient{std::move(opts)};
}

TEST(McpClient, Construction) {
    auto client = make_test_client();
    EXPECT_FALSE(client.is_connected());
}

TEST(McpClient, RegisterCallbacks) {
    auto client = make_test_client();
    bool tools_changed = false;
    client.on_tools_changed([&tools_changed]() { tools_changed = true; });
    client.on_resources_changed([]() {});
    client.on_prompts_changed([]() {});
    client.on_log_message([](const LogMessage&) {});
    // No exception
    SUCCEED();
}

TEST(McpClient, RegisterServerToClientHandlers) {
    auto client = make_test_client();
    client.on_sampling_request([](const SamplingRequest& req) -> SamplingResult {
        SamplingResult result;
        result.role = "assistant";
        result.content = TextContent{"response", std::nullopt};
        result.model = "test-model";
        return result;
    });
    client.on_roots_request([]() -> std::vector<Root> {
        return {{"file:///home", "Home"}};
    });
    client.on_elicitation_request([](const ElicitationRequest&) -> ElicitationResult {
        return {"accept", nlohmann::json{{"name", "John"}}};
    });
    SUCCEED();
}

TEST(McpClient, DisconnectWhenNotConnected) {
    auto client = make_test_client();
    client.disconnect();
    EXPECT_FALSE(client.is_connected());
}
