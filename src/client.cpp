#include "mcp/client.hpp"
#include "mcp/codec.hpp"
#include "mcp/session.hpp"
#include "mcp/router.hpp"
#include "mcp/error.hpp"
#include "mcp/version.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include "mcp/transport/http_transport.hpp"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mcp {

struct McpClient::Impl {
    Options opts;
    Session session;
    Router router;

    std::unique_ptr<ITransport> transport;
    std::thread transport_thread;
    std::atomic<bool> connected{false};

    // Pending request map: id_string -> promise
    std::mutex pending_mutex;
    std::unordered_map<std::string, std::promise<JsonRpcResponse>> pending_responses;
    int64_t next_id{1};

    // Notification callbacks
    std::mutex callback_mutex;
    std::function<void()> on_tools_changed_cb;
    std::function<void()> on_resources_changed_cb;
    std::function<void(const std::string&)> on_resource_updated_cb;
    std::function<void()> on_prompts_changed_cb;
    std::function<void(const LogMessage&)> on_log_message_cb;

    // Server->client request handlers
    std::function<SamplingResult(const SamplingRequest&)> sampling_handler;
    std::function<std::vector<Root>()> roots_handler;
    std::function<ElicitationResult(const ElicitationRequest&)> elicitation_handler;

    explicit Impl(Options o) : opts(std::move(o)) {}

    std::string id_to_key(const RequestId& id) {
        if (auto* i = std::get_if<int64_t>(&id)) return std::to_string(*i);
        if (auto* s = std::get_if<std::string>(&id)) return *s;
        return "";
    }

    void setup_notification_handlers() {
        router.on_notification("notifications/tools/list_changed", [this](const nlohmann::json&) {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (on_tools_changed_cb) on_tools_changed_cb();
        });
        router.on_notification("notifications/resources/list_changed", [this](const nlohmann::json&) {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (on_resources_changed_cb) on_resources_changed_cb();
        });
        router.on_notification("notifications/resources/updated", [this](const nlohmann::json& params) {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (on_resource_updated_cb) {
                std::string uri = params.value("uri", "");
                on_resource_updated_cb(uri);
            }
        });
        router.on_notification("notifications/prompts/list_changed", [this](const nlohmann::json&) {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (on_prompts_changed_cb) on_prompts_changed_cb();
        });
        router.on_notification("notifications/message", [this](const nlohmann::json& params) {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (on_log_message_cb) {
                try {
                    LogMessage msg = params.get<LogMessage>();
                    on_log_message_cb(msg);
                } catch (...) {}
            }
        });
        router.on_notification("notifications/progress", [](const nlohmann::json&) {
            // Progress notifications - user can extend
        });
        router.on_notification("notifications/cancelled", [](const nlohmann::json&) {
            // Handle cancelled notification
        });

        // Server->client request handlers
        router.on_request("sampling/createMessage", [this](const nlohmann::json& params) -> HandlerResult {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (!sampling_handler) {
                return JsonRpcError{error::MethodNotFound, "No sampling handler registered", std::nullopt};
            }
            try {
                SamplingRequest req = params.get<SamplingRequest>();
                SamplingResult result = sampling_handler(req);
                nlohmann::json j;
                to_json(j, result);
                return j;
            } catch (const std::exception& e) {
                return JsonRpcError{error::InternalError, e.what(), std::nullopt};
            }
        });

        router.on_request("roots/list", [this](const nlohmann::json&) -> HandlerResult {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (!roots_handler) {
                return JsonRpcError{error::MethodNotFound, "No roots handler registered", std::nullopt};
            }
            try {
                auto roots = roots_handler();
                return nlohmann::json{{"roots", roots}};
            } catch (const std::exception& e) {
                return JsonRpcError{error::InternalError, e.what(), std::nullopt};
            }
        });

        router.on_request("elicitation/create", [this](const nlohmann::json& params) -> HandlerResult {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (!elicitation_handler) {
                return JsonRpcError{error::MethodNotFound, "No elicitation handler registered", std::nullopt};
            }
            try {
                ElicitationRequest req = params.get<ElicitationRequest>();
                ElicitationResult result = elicitation_handler(req);
                nlohmann::json j;
                to_json(j, result);
                return j;
            } catch (const std::exception& e) {
                return JsonRpcError{error::InternalError, e.what(), std::nullopt};
            }
        });
    }

    void on_message(JsonRpcMessage msg) {
        if (auto* resp = std::get_if<JsonRpcResponse>(&msg)) {
            // Match to pending request
            std::string key = id_to_key(resp->id);
            std::lock_guard<std::mutex> lock(pending_mutex);
            auto it = pending_responses.find(key);
            if (it != pending_responses.end()) {
                it->second.set_value(*resp);
                pending_responses.erase(it);
            }
            return;
        }

        // Dispatch notifications and server->client requests
        auto response = router.dispatch(msg);
        if (response && transport) {
            transport->send(*response);
        }
    }

    JsonRpcResponse send_request(const std::string& method, nlohmann::json params) {
        if (!connected) {
            throw McpTransportError("Not connected");
        }

        int64_t id;
        std::future<JsonRpcResponse> fut;
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            id = next_id++;
            auto& p = pending_responses[std::to_string(id)];
            fut = p.get_future();
        }

        JsonRpcRequest req;
        req.id = RequestId{id};
        req.method = method;
        req.params = std::move(params);
        transport->send(req);

        if (fut.wait_for(opts.request_timeout) == std::future_status::timeout) {
            std::lock_guard<std::mutex> lock(pending_mutex);
            pending_responses.erase(std::to_string(id));
            throw McpTimeoutError("Request timed out: " + method);
        }

        return fut.get();
    }

    void do_connect(std::unique_ptr<ITransport> t) {
        transport = std::move(t);
        setup_notification_handlers();
        connected = true;
        session.set_state(SessionState::Uninitialized);

        transport_thread = std::thread([this]() {
            transport->start([this](JsonRpcMessage msg) {
                on_message(std::move(msg));
            });
            connected = false;
        });
    }
};

McpClient::McpClient(Options opts)
    : impl_(std::make_unique<Impl>(std::move(opts))) {}

McpClient::~McpClient() {
    disconnect();
}

void McpClient::connect_stdio(const std::string& command,
                               const std::vector<std::string>& args) {
    // Launch subprocess and create pipe-based transport
    // Use popen-style or fork/exec
    // For simplicity, use a pipe approach via posix_spawn or fork/exec
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) {
        throw McpTransportError("Failed to create pipes");
    }

    pid_t pid = fork();
    if (pid < 0) {
        throw McpTransportError("Failed to fork process");
    }
    if (pid == 0) {
        // Child: redirect stdin/stdout
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);

        // Build argv
        std::vector<char*> argv_vec;
        std::string cmd = command;
        argv_vec.push_back(cmd.data());
        std::vector<std::string> args_copy = args;
        for (auto& a : args_copy) argv_vec.push_back(a.data());
        argv_vec.push_back(nullptr);

        execvp(command.c_str(), argv_vec.data());
        _exit(1);
    }

    // Parent
    close(in_pipe[0]);
    close(out_pipe[1]);

    // write_fd = in_pipe[1] (we write to child's stdin)
    // read_fd  = out_pipe[0] (we read from child's stdout)
    auto transport = std::make_unique<StdioTransport>(out_pipe[0], in_pipe[1]);
    impl_->do_connect(std::move(transport));
}

void McpClient::connect_http(const std::string& url) {
    auto transport = std::make_unique<HttpClientTransport>(url);
    impl_->do_connect(std::move(transport));
}

void McpClient::connect(std::unique_ptr<ITransport> transport) {
    impl_->do_connect(std::move(transport));
}

void McpClient::disconnect() {
    if (impl_->transport) {
        impl_->transport->shutdown();
    }
    if (impl_->transport_thread.joinable()) {
        impl_->transport_thread.join();
    }
    impl_->connected = false;
    impl_->session.set_state(SessionState::Closed);
}

InitializeResult McpClient::initialize() {
    nlohmann::json params = {
        {"protocolVersion", std::string(PROTOCOL_VERSION)},
        {"clientInfo", impl_->opts.client_info},
        {"capabilities", impl_->opts.capabilities}
    };

    impl_->session.set_state(SessionState::Initializing);
    auto resp = impl_->send_request("initialize", params);

    if (resp.error) {
        throw McpProtocolError(resp.error->code, resp.error->message);
    }

    InitializeResult result;
    from_json(*resp.result, result);

    impl_->session.server_capabilities() = result.capabilities;
    impl_->session.protocol_version() = result.protocol_version;
    impl_->router.set_capabilities(result.capabilities, impl_->opts.capabilities);
    impl_->session.set_state(SessionState::Ready);

    // Send initialized notification
    JsonRpcNotification notif;
    notif.method = "notifications/initialized";
    impl_->transport->send(notif);

    return result;
}

PaginatedResult<ToolDefinition> McpClient::list_tools(std::optional<std::string> cursor) {
    nlohmann::json params = nlohmann::json::object();
    if (cursor) params["cursor"] = *cursor;

    auto resp = impl_->send_request("tools/list", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);

    PaginatedResult<ToolDefinition> result;
    result.items = resp.result->at("tools").get<std::vector<ToolDefinition>>();
    if (resp.result->contains("nextCursor") && !resp.result->at("nextCursor").is_null()) {
        result.next_cursor = resp.result->at("nextCursor").get<std::string>();
    }
    return result;
}

CallToolResult McpClient::call_tool(const std::string& name, const nlohmann::json& arguments) {
    nlohmann::json params = {{"name", name}, {"arguments", arguments}};
    auto resp = impl_->send_request("tools/call", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);

    CallToolResult result;
    from_json(*resp.result, result);
    return result;
}

PaginatedResult<ResourceDefinition> McpClient::list_resources(std::optional<std::string> cursor) {
    nlohmann::json params = nlohmann::json::object();
    if (cursor) params["cursor"] = *cursor;

    auto resp = impl_->send_request("resources/list", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);

    PaginatedResult<ResourceDefinition> result;
    result.items = resp.result->at("resources").get<std::vector<ResourceDefinition>>();
    if (resp.result->contains("nextCursor") && !resp.result->at("nextCursor").is_null()) {
        result.next_cursor = resp.result->at("nextCursor").get<std::string>();
    }
    return result;
}

std::vector<ResourceContent> McpClient::read_resource(const std::string& uri) {
    nlohmann::json params = {{"uri", uri}};
    auto resp = impl_->send_request("resources/read", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);

    return resp.result->at("contents").get<std::vector<ResourceContent>>();
}

PaginatedResult<ResourceTemplate> McpClient::list_resource_templates(
    std::optional<std::string> cursor) {
    nlohmann::json params = nlohmann::json::object();
    if (cursor) params["cursor"] = *cursor;

    auto resp = impl_->send_request("resources/templates/list", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);

    PaginatedResult<ResourceTemplate> result;
    result.items = resp.result->at("resourceTemplates").get<std::vector<ResourceTemplate>>();
    if (resp.result->contains("nextCursor") && !resp.result->at("nextCursor").is_null()) {
        result.next_cursor = resp.result->at("nextCursor").get<std::string>();
    }
    return result;
}

void McpClient::subscribe_resource(const std::string& uri) {
    auto resp = impl_->send_request("resources/subscribe", {{"uri", uri}});
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);
}

void McpClient::unsubscribe_resource(const std::string& uri) {
    auto resp = impl_->send_request("resources/unsubscribe", {{"uri", uri}});
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);
}

PaginatedResult<PromptDefinition> McpClient::list_prompts(std::optional<std::string> cursor) {
    nlohmann::json params = nlohmann::json::object();
    if (cursor) params["cursor"] = *cursor;

    auto resp = impl_->send_request("prompts/list", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);

    PaginatedResult<PromptDefinition> result;
    result.items = resp.result->at("prompts").get<std::vector<PromptDefinition>>();
    if (resp.result->contains("nextCursor") && !resp.result->at("nextCursor").is_null()) {
        result.next_cursor = resp.result->at("nextCursor").get<std::string>();
    }
    return result;
}

GetPromptResult McpClient::get_prompt(const std::string& name, const nlohmann::json& arguments) {
    nlohmann::json params = {{"name", name}, {"arguments", arguments}};
    auto resp = impl_->send_request("prompts/get", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);

    GetPromptResult result;
    from_json(*resp.result, result);
    return result;
}

CompletionResult McpClient::complete(const CompletionRef& ref, const std::string& arg_name,
                                      const std::string& arg_value) {
    nlohmann::json ref_j;
    to_json(ref_j, ref);
    nlohmann::json params = {
        {"ref", ref_j},
        {"argument", {{"name", arg_name}, {"value", arg_value}}}
    };
    auto resp = impl_->send_request("completion/complete", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);

    CompletionResult result;
    from_json(*resp.result, result);
    return result;
}

void McpClient::set_log_level(LogLevel level) {
    nlohmann::json params = {{"level", level}};
    auto resp = impl_->send_request("logging/setLevel", params);
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);
}

void McpClient::on_log_message(std::function<void(const LogMessage&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->on_log_message_cb = std::move(callback);
}

void McpClient::on_tools_changed(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->on_tools_changed_cb = std::move(callback);
}

void McpClient::on_resources_changed(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->on_resources_changed_cb = std::move(callback);
}

void McpClient::on_resource_updated(std::function<void(const std::string& uri)> callback) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->on_resource_updated_cb = std::move(callback);
}

void McpClient::on_prompts_changed(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->on_prompts_changed_cb = std::move(callback);
}

void McpClient::cancel_request(const RequestId& id, const std::string& reason) {
    nlohmann::json params;
    std::visit([&params](const auto& v) { params["requestId"] = v; }, id);
    if (!reason.empty()) params["reason"] = reason;

    JsonRpcNotification notif;
    notif.method = "notifications/cancelled";
    notif.params = params;
    impl_->transport->send(notif);
}

void McpClient::ping() {
    auto resp = impl_->send_request("ping", nlohmann::json::object());
    if (resp.error) throw McpProtocolError(resp.error->code, resp.error->message);
}

void McpClient::on_sampling_request(std::function<SamplingResult(const SamplingRequest&)> handler) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->sampling_handler = std::move(handler);
}

void McpClient::on_roots_request(std::function<std::vector<Root>()> handler) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->roots_handler = std::move(handler);
}

void McpClient::on_elicitation_request(
    std::function<ElicitationResult(const ElicitationRequest&)> handler) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->elicitation_handler = std::move(handler);
}

bool McpClient::is_connected() const {
    return impl_->connected;
}

const ServerCapabilities& McpClient::server_capabilities() const {
    return impl_->session.server_capabilities();
}

} // namespace mcp
