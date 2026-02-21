/// Full-featured MCP server demonstrating all capabilities.
/// Usage: ./full_featured_server

#include <mcp/mcp.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    mcp::McpServer::Options opts;
    opts.server_info = {"full-featured-server", std::nullopt, "1.0.0"};
    opts.instructions = "A full-featured MCP server demonstrating all capabilities.";
    opts.thread_pool_size = 4;
    opts.request_timeout = std::chrono::milliseconds(30000);

    mcp::McpServer server{std::move(opts)};

    // ---- Tools ----

    // Echo tool
    mcp::ToolDefinition echo_def;
    echo_def.name = "echo";
    echo_def.description = "Echo text back";
    echo_def.input_schema = {
        {"type", "object"},
        {"properties", {{"text", {{"type", "string"}}}}},
        {"required", {"text"}}
    };
    server.add_tool(echo_def, [](const nlohmann::json& args) -> mcp::CallToolResult {
        mcp::CallToolResult r;
        r.content.push_back(mcp::TextContent{args.at("text").get<std::string>(), std::nullopt});
        return r;
    });

    // Progress tool — demonstrates progress notifications
    mcp::ToolDefinition progress_def;
    progress_def.name = "long_task";
    progress_def.description = "A task that reports progress";
    progress_def.input_schema = {
        {"type", "object"},
        {"properties", {{"steps", {{"type", "integer"}, {"default", 5}}}}},
    };
    server.add_tool(progress_def, [&server](const nlohmann::json& args) -> mcp::CallToolResult {
        int steps = args.value("steps", 5);
        int64_t token = 42;
        for (int i = 1; i <= steps; ++i) {
            server.send_progress(
                std::variant<int64_t, std::string>{token},
                static_cast<double>(i),
                static_cast<double>(steps),
                "Step " + std::to_string(i) + " of " + std::to_string(steps)
            );
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        mcp::CallToolResult r;
        r.content.push_back(mcp::TextContent{"Task completed!", std::nullopt});
        return r;
    });

    // Structured output tool
    mcp::ToolDefinition weather_def;
    weather_def.name = "get_weather";
    weather_def.description = "Get weather for a location";
    weather_def.input_schema = {
        {"type", "object"},
        {"properties", {{"location", {{"type", "string"}}}}},
        {"required", {"location"}}
    };
    weather_def.output_schema = {
        {"type", "object"},
        {"properties", {
            {"temperature", {{"type", "number"}}},
            {"condition", {{"type", "string"}}},
            {"humidity", {{"type", "number"}}}
        }}
    };
    server.add_tool(weather_def, [](const nlohmann::json& args) -> mcp::CallToolResult {
        std::string location = args.at("location").get<std::string>();
        mcp::CallToolResult r;
        r.content.push_back(mcp::TextContent{
            "Weather in " + location + ": Sunny, 22°C", std::nullopt});
        r.structured_content = {
            {"temperature", 22.0},
            {"condition", "Sunny"},
            {"humidity", 65.0}
        };
        return r;
    });

    // ---- Resources ----

    mcp::ResourceDefinition status_resource;
    status_resource.uri = "app://status";
    status_resource.name = "Server Status";
    status_resource.mime_type = "application/json";
    server.add_resource(status_resource, [](const std::string& uri) -> std::vector<mcp::ResourceContent> {
        nlohmann::json status = {
            {"status", "running"},
            {"uptime", "0h 0m"},
            {"version", "1.0.0"}
        };
        return {mcp::ResourceContent{uri, std::string("application/json"),
                status.dump(), std::nullopt}};
    });

    // ---- Prompts ----

    mcp::PromptDefinition assist_def;
    assist_def.name = "assistant";
    assist_def.description = "Get an AI assistant response";
    assist_def.arguments = {{"query", std::string("Your question"), true}};
    server.add_prompt(assist_def,
        [](const std::string&, const nlohmann::json& args) -> mcp::GetPromptResult {
            mcp::GetPromptResult result;
            result.messages.push_back({"user",
                mcp::TextContent{args.at("query").get<std::string>(), std::nullopt}});
            return result;
        });

    // ---- Completions ----

    server.set_completion_handler(
        [](const mcp::CompletionRef&, const std::string&,
           const std::string& val) -> mcp::CompletionResult {
            return {{"option1", "option2", "option3"}, std::nullopt, false};
        });

    // Log startup
    server.log(mcp::LogLevel::Info, "full-server",
               nlohmann::json("Full-featured server started with all capabilities"));

    server.serve_stdio();
    return 0;
}
