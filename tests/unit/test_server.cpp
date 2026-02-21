#include <gtest/gtest.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/codec.hpp"
#include <thread>
#include <chrono>

using namespace mcp;

// Helper: create a minimal server for testing
McpServer make_test_server() {
    McpServer::Options opts;
    opts.server_info = {"test-server", std::nullopt, "1.0.0"};
    opts.thread_pool_size = 2;
    return McpServer{std::move(opts)};
}

TEST(McpServer, Construction) {
    auto server = make_test_server();
    EXPECT_FALSE(server.is_running());
}

TEST(McpServer, AddTool) {
    auto server = make_test_server();
    ToolDefinition def;
    def.name = "echo";
    def.input_schema = nlohmann::json{{"type", "object"}};

    bool added = false;
    server.add_tool(std::move(def), [&added](const nlohmann::json&) -> CallToolResult {
        added = true;
        CallToolResult result;
        result.content.push_back(TextContent{"pong", std::nullopt});
        return result;
    });
    // Tool is registered - no exception
    SUCCEED();
}

TEST(McpServer, AddAndRemoveTool) {
    auto server = make_test_server();
    ToolDefinition def;
    def.name = "temp_tool";
    def.input_schema = nlohmann::json{{"type", "object"}};

    server.add_tool(def, [](const nlohmann::json&) -> CallToolResult { return {}; });
    server.remove_tool("temp_tool");
    // No exception - success
    SUCCEED();
}

TEST(McpServer, AddResource) {
    auto server = make_test_server();
    ResourceDefinition def;
    def.uri = "file:///test.txt";
    def.name = "Test File";

    server.add_resource(def, [](const std::string& uri) -> std::vector<ResourceContent> {
        return {ResourceContent{uri, std::nullopt, std::string("content"), std::nullopt}};
    });
    SUCCEED();
}

TEST(McpServer, AddPrompt) {
    auto server = make_test_server();
    PromptDefinition def;
    def.name = "test_prompt";
    def.arguments = {{"arg1", std::nullopt, true}};

    server.add_prompt(def, [](const std::string& name, const nlohmann::json& args) -> GetPromptResult {
        GetPromptResult result;
        result.messages.push_back({"user", TextContent{args.value("arg1", ""), std::nullopt}});
        return result;
    });
    SUCCEED();
}

TEST(McpServer, Shutdown) {
    McpServer::Options opts;
    opts.server_info = {"test", std::nullopt, "1.0"};
    McpServer server{std::move(opts)};
    // Should not throw even if not running
    server.shutdown();
    SUCCEED();
}
