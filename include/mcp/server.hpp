#pragma once
#include "types.hpp"
#include "json_rpc.hpp"
#include "transport/transport.hpp"
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace mcp {

/// Callback types
using ToolHandler = std::function<CallToolResult(const nlohmann::json& arguments)>;
using ResourceReadHandler = std::function<std::vector<ResourceContent>(const std::string& uri)>;
using PromptGetHandler = std::function<GetPromptResult(const std::string& name,
                                                        const nlohmann::json& arguments)>;
using CompletionHandler = std::function<CompletionResult(const CompletionRef& ref,
                                                          const std::string& arg_name,
                                                          const std::string& arg_value)>;
using AsyncToolHandler = std::function<std::future<CallToolResult>(const nlohmann::json& arguments)>;

class McpServer {
public:
    struct Options {
        Implementation server_info;
        std::optional<std::string> instructions;
        int thread_pool_size = 4;
        std::chrono::milliseconds request_timeout{30000};
        size_t page_size = 50;
    };

    explicit McpServer(Options opts);
    ~McpServer();

    // Non-copyable, non-movable
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    // ---- Tool registration ----
    void add_tool(ToolDefinition def, ToolHandler handler);
    void add_tool_async(ToolDefinition def, AsyncToolHandler handler);
    void remove_tool(const std::string& name);

    // ---- Resource registration ----
    void add_resource(ResourceDefinition def, ResourceReadHandler handler);
    void add_resource_template(ResourceTemplate tmpl, ResourceReadHandler handler);
    void notify_resource_updated(const std::string& uri);
    void remove_resource(const std::string& uri);

    // ---- Prompt registration ----
    void add_prompt(PromptDefinition def, PromptGetHandler handler);
    void remove_prompt(const std::string& name);

    // ---- Completion ----
    void set_completion_handler(CompletionHandler handler);

    // ---- Logging ----
    void log(LogLevel level, const std::string& logger, const nlohmann::json& data);

    // ---- Progress ----
    void send_progress(const std::variant<int64_t, std::string>& token,
                       double progress, std::optional<double> total = std::nullopt,
                       std::optional<std::string> message = std::nullopt);

    // ---- Sampling (server->client) ----
    SamplingResult request_sampling(const SamplingRequest& req);
    std::future<SamplingResult> request_sampling_async(const SamplingRequest& req);

    // ---- Elicitation (server->client) ----
    ElicitationResult request_elicitation(const ElicitationRequest& req);

    // ---- Roots (server->client) ----
    std::vector<Root> request_roots();

    // ---- Transport ----
    void serve_stdio();
    void serve_http(const std::string& host, uint16_t port);
    void serve(std::unique_ptr<ITransport> transport);
    void shutdown();

    bool is_running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mcp
