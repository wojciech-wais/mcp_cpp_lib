#pragma once
#include "types.hpp"
#include "json_rpc.hpp"
#include "transport/transport.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace mcp {

class McpClient {
public:
    struct Options {
        Implementation client_info;
        ClientCapabilities capabilities;
        std::chrono::milliseconds request_timeout{30000};
    };

    explicit McpClient(Options opts);
    ~McpClient();

    McpClient(const McpClient&) = delete;
    McpClient& operator=(const McpClient&) = delete;

    // ---- Connection ----
    void connect_stdio(const std::string& command,
                       const std::vector<std::string>& args = {});
    void connect_http(const std::string& url);
    void connect(std::unique_ptr<ITransport> transport);
    void disconnect();

    // ---- Initialization ----
    InitializeResult initialize();

    // ---- Tools ----
    PaginatedResult<ToolDefinition> list_tools(std::optional<std::string> cursor = std::nullopt);
    CallToolResult call_tool(const std::string& name,
                              const nlohmann::json& arguments = nlohmann::json::object());

    // ---- Resources ----
    PaginatedResult<ResourceDefinition> list_resources(std::optional<std::string> cursor = std::nullopt);
    std::vector<ResourceContent> read_resource(const std::string& uri);
    PaginatedResult<ResourceTemplate> list_resource_templates(
        std::optional<std::string> cursor = std::nullopt);
    void subscribe_resource(const std::string& uri);
    void unsubscribe_resource(const std::string& uri);

    // ---- Prompts ----
    PaginatedResult<PromptDefinition> list_prompts(std::optional<std::string> cursor = std::nullopt);
    GetPromptResult get_prompt(const std::string& name,
                                const nlohmann::json& arguments = nlohmann::json::object());

    // ---- Completion ----
    CompletionResult complete(const CompletionRef& ref, const std::string& arg_name,
                              const std::string& arg_value);

    // ---- Logging ----
    void set_log_level(LogLevel level);
    void on_log_message(std::function<void(const LogMessage&)> callback);

    // ---- Notifications ----
    void on_tools_changed(std::function<void()> callback);
    void on_resources_changed(std::function<void()> callback);
    void on_resource_updated(std::function<void(const std::string& uri)> callback);
    void on_prompts_changed(std::function<void()> callback);

    // ---- Cancellation ----
    void cancel_request(const RequestId& id, const std::string& reason = "");

    // ---- Ping ----
    void ping();

    // ---- Callbacks for server->client requests ----
    void on_sampling_request(std::function<SamplingResult(const SamplingRequest&)> handler);
    void on_roots_request(std::function<std::vector<Root>()> handler);
    void on_elicitation_request(std::function<ElicitationResult(const ElicitationRequest&)> handler);

    // ---- State ----
    bool is_connected() const;
    const ServerCapabilities& server_capabilities() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mcp
