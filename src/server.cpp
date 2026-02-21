#include "mcp/server.hpp"
#include "mcp/codec.hpp"
#include "mcp/session.hpp"
#include "mcp/router.hpp"
#include "mcp/error.hpp"
#include "mcp/version.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include "mcp/transport/http_transport.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

namespace mcp {

// ----------- Pager helper -----------

template<typename T>
struct PagedStore {
    std::vector<T> items;
    size_t page_size = 50;

    // returns items starting from cursor, and next_cursor
    std::pair<std::vector<T>, std::optional<std::string>>
    page(const std::optional<std::string>& cursor) const {
        size_t start = 0;
        if (cursor) {
            try { start = std::stoull(*cursor); } catch (...) { start = 0; }
        }
        if (start >= items.size()) {
            return {{}, std::nullopt};
        }
        size_t end = std::min(start + page_size, items.size());
        std::vector<T> page_items(items.begin() + start, items.begin() + end);
        std::optional<std::string> next;
        if (end < items.size()) next = std::to_string(end);
        return {page_items, next};
    }
};

// ----------- McpServer::Impl -----------

struct McpServer::Impl {
    Options opts;
    Session session;
    Router router;

    // Storage
    std::mutex store_mutex;
    PagedStore<ToolDefinition> tools;
    std::unordered_map<std::string, ToolHandler> tool_handlers;
    std::unordered_map<std::string, AsyncToolHandler> async_tool_handlers;

    PagedStore<ResourceDefinition> resources;
    std::unordered_map<std::string, ResourceReadHandler> resource_handlers;

    PagedStore<ResourceTemplate> resource_templates;
    std::unordered_map<std::string, ResourceReadHandler> resource_template_handlers;

    PagedStore<PromptDefinition> prompts;
    std::unordered_map<std::string, PromptGetHandler> prompt_handlers;

    std::optional<CompletionHandler> completion_handler;

    // Subscriptions (resource URI -> set of session IDs)
    std::set<std::string> subscribed_uris;

    // Transport reference for sending outbound messages
    ITransport* transport{nullptr};
    std::mutex transport_mutex;

    std::atomic<bool> running{false};
    std::atomic<LogLevel> min_log_level{LogLevel::Info};

    // Thread pool
    std::vector<std::thread> thread_pool;
    std::queue<std::function<void()>> task_queue;
    std::mutex pool_mutex;
    std::condition_variable pool_cv;
    std::atomic<bool> pool_running{false};

    // Pending server->client requests
    std::mutex pending_mutex;
    std::unordered_map<std::string, std::promise<JsonRpcResponse>> pending_responses;
    int64_t next_outbound_id{1};

    explicit Impl(Options o) : opts(std::move(o)) {}

    void start_thread_pool() {
        pool_running = true;
        for (int i = 0; i < opts.thread_pool_size; ++i) {
            thread_pool.emplace_back([this] {
                while (pool_running) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(pool_mutex);
                        pool_cv.wait(lock, [this] {
                            return !task_queue.empty() || !pool_running;
                        });
                        if (!pool_running && task_queue.empty()) return;
                        task = std::move(task_queue.front());
                        task_queue.pop();
                    }
                    task();
                }
            });
        }
    }

    void stop_thread_pool() {
        pool_running = false;
        pool_cv.notify_all();
        for (auto& t : thread_pool) {
            if (t.joinable()) t.join();
        }
        thread_pool.clear();
    }

    void dispatch_to_pool(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(pool_mutex);
            task_queue.push(std::move(fn));
        }
        pool_cv.notify_one();
    }

    void send_message(const JsonRpcMessage& msg) {
        std::lock_guard<std::mutex> lock(transport_mutex);
        if (transport) {
            transport->send(msg);
        }
    }

    void send_notification(const std::string& method,
                           std::optional<nlohmann::json> params = std::nullopt) {
        JsonRpcNotification notif;
        notif.method = method;
        notif.params = std::move(params);
        send_message(notif);
    }

    // Build server capabilities from what's registered
    ServerCapabilities build_capabilities() const {
        ServerCapabilities caps;
        {
            if (!tools.items.empty()) {
                caps.tools = nlohmann::json{{"listChanged", true}};
            }
            if (!resources.items.empty() || !resource_templates.items.empty()) {
                caps.resources = nlohmann::json{
                    {"subscribe", true},
                    {"listChanged", true}
                };
            }
            if (!prompts.items.empty()) {
                caps.prompts = nlohmann::json{{"listChanged", true}};
            }
            caps.logging = nlohmann::json::object();
            if (completion_handler) {
                caps.completions = nlohmann::json::object();
            }
        }
        return caps;
    }

    void setup_handlers() {
        // initialize
        router.on_request("initialize", [this](const nlohmann::json& params) -> HandlerResult {
            // Parse client info
            if (params.contains("clientInfo")) {
                try {
                    session.client_capabilities() = params.value("capabilities", nlohmann::json::object()).get<ClientCapabilities>();
                } catch (...) {}
            }

            std::string client_proto = params.value("protocolVersion", std::string(PROTOCOL_VERSION));

            // Negotiate protocol version - we accept the client's if we support it
            std::string negotiated = std::string(PROTOCOL_VERSION);
            session.protocol_version() = negotiated;

            session.set_state(SessionState::Initializing);

            ServerCapabilities caps;
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                caps = build_capabilities();
            }
            session.server_capabilities() = caps;

            InitializeResult result;
            result.protocol_version = negotiated;
            result.capabilities = caps;
            result.server_info = opts.server_info;
            result.instructions = opts.instructions;

            nlohmann::json j;
            to_json(j, result);
            return j;
        });

        // notifications/initialized
        router.on_notification("notifications/initialized", [this](const nlohmann::json&) {
            session.set_state(SessionState::Ready);
            // Update router capabilities
            router.set_capabilities(session.server_capabilities(), session.client_capabilities());
        });

        // ping
        router.on_request("ping", [](const nlohmann::json&) -> HandlerResult {
            return nlohmann::json::object();
        });

        // tools/list
        router.on_request("tools/list", [this](const nlohmann::json& params) -> HandlerResult {
            std::optional<std::string> cursor;
            if (params.contains("cursor") && !params.at("cursor").is_null()) {
                cursor = params.at("cursor").get<std::string>();
            }
            std::lock_guard<std::mutex> lock(store_mutex);
            auto [items, next] = tools.page(cursor);
            nlohmann::json result = {{"tools", items}};
            if (next) result["nextCursor"] = *next;
            return result;
        });

        // tools/call
        router.on_request("tools/call", [this](const nlohmann::json& params) -> HandlerResult {
            std::string name = params.at("name").get<std::string>();
            nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

            ToolHandler* handler = nullptr;
            AsyncToolHandler* async_handler = nullptr;
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                auto it = tool_handlers.find(name);
                if (it != tool_handlers.end()) {
                    handler = &it->second;
                } else {
                    auto ait = async_tool_handlers.find(name);
                    if (ait != async_tool_handlers.end()) {
                        async_handler = &ait->second;
                    }
                }
            }

            if (!handler && !async_handler) {
                return JsonRpcError{error::InvalidParams, "Unknown tool: " + name, std::nullopt};
            }

            try {
                CallToolResult tool_result;
                if (handler) {
                    tool_result = (*handler)(arguments);
                } else {
                    auto fut = (*async_handler)(arguments);
                    tool_result = fut.get();
                }
                nlohmann::json j;
                to_json(j, tool_result);
                return j;
            } catch (const std::exception& e) {
                CallToolResult error_result;
                error_result.is_error = true;
                error_result.content.push_back(TextContent{e.what(), std::nullopt});
                nlohmann::json j;
                to_json(j, error_result);
                return j;
            }
        });

        // resources/list
        router.on_request("resources/list", [this](const nlohmann::json& params) -> HandlerResult {
            std::optional<std::string> cursor;
            if (params.contains("cursor") && !params.at("cursor").is_null()) {
                cursor = params.at("cursor").get<std::string>();
            }
            std::lock_guard<std::mutex> lock(store_mutex);
            auto [items, next] = resources.page(cursor);
            nlohmann::json result = {{"resources", items}};
            if (next) result["nextCursor"] = *next;
            return result;
        });

        // resources/read
        router.on_request("resources/read", [this](const nlohmann::json& params) -> HandlerResult {
            std::string uri = params.at("uri").get<std::string>();

            ResourceReadHandler* handler = nullptr;
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                auto it = resource_handlers.find(uri);
                if (it != resource_handlers.end()) {
                    handler = &it->second;
                } else {
                    // Try templates - find a matching template handler
                    for (auto& [tmpl_key, tmpl_handler] : resource_template_handlers) {
                        // Simple prefix match
                        if (uri.find(tmpl_key.substr(0, tmpl_key.find('{'))) == 0) {
                            handler = &tmpl_handler;
                            break;
                        }
                    }
                }
            }

            if (!handler) {
                return JsonRpcError{error::ResourceNotFound, "Resource not found: " + uri, std::nullopt};
            }

            try {
                auto contents = (*handler)(uri);
                nlohmann::json result = {{"contents", contents}};
                return result;
            } catch (const std::exception& e) {
                return JsonRpcError{error::InternalError, e.what(), std::nullopt};
            }
        });

        // resources/templates/list
        router.on_request("resources/templates/list", [this](const nlohmann::json& params) -> HandlerResult {
            std::optional<std::string> cursor;
            if (params.contains("cursor") && !params.at("cursor").is_null()) {
                cursor = params.at("cursor").get<std::string>();
            }
            std::lock_guard<std::mutex> lock(store_mutex);
            auto [items, next] = resource_templates.page(cursor);
            nlohmann::json result = {{"resourceTemplates", items}};
            if (next) result["nextCursor"] = *next;
            return result;
        });

        // resources/subscribe
        router.on_request("resources/subscribe", [this](const nlohmann::json& params) -> HandlerResult {
            std::string uri = params.at("uri").get<std::string>();
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                subscribed_uris.insert(uri);
            }
            return nlohmann::json::object();
        });

        // resources/unsubscribe
        router.on_request("resources/unsubscribe", [this](const nlohmann::json& params) -> HandlerResult {
            std::string uri = params.at("uri").get<std::string>();
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                subscribed_uris.erase(uri);
            }
            return nlohmann::json::object();
        });

        // prompts/list
        router.on_request("prompts/list", [this](const nlohmann::json& params) -> HandlerResult {
            std::optional<std::string> cursor;
            if (params.contains("cursor") && !params.at("cursor").is_null()) {
                cursor = params.at("cursor").get<std::string>();
            }
            std::lock_guard<std::mutex> lock(store_mutex);
            auto [items, next] = prompts.page(cursor);
            nlohmann::json result = {{"prompts", items}};
            if (next) result["nextCursor"] = *next;
            return result;
        });

        // prompts/get
        router.on_request("prompts/get", [this](const nlohmann::json& params) -> HandlerResult {
            std::string name = params.at("name").get<std::string>();
            nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

            PromptGetHandler* handler = nullptr;
            {
                std::lock_guard<std::mutex> lock(store_mutex);
                auto it = prompt_handlers.find(name);
                if (it != prompt_handlers.end()) {
                    handler = &it->second;
                }
            }

            if (!handler) {
                return JsonRpcError{error::InvalidParams, "Unknown prompt: " + name, std::nullopt};
            }

            try {
                auto result = (*handler)(name, arguments);
                nlohmann::json j;
                to_json(j, result);
                return j;
            } catch (const std::exception& e) {
                return JsonRpcError{error::InternalError, e.what(), std::nullopt};
            }
        });

        // completion/complete
        router.on_request("completion/complete", [this](const nlohmann::json& params) -> HandlerResult {
            if (!completion_handler) {
                return JsonRpcError{error::MethodNotFound, "No completion handler registered", std::nullopt};
            }

            CompletionRef ref = params.at("ref").get<CompletionRef>();
            std::string arg_name = params.at("argument").at("name").get<std::string>();
            std::string arg_value = params.at("argument").at("value").get<std::string>();

            try {
                auto result = (*completion_handler)(ref, arg_name, arg_value);
                nlohmann::json j;
                to_json(j, result);
                return j;
            } catch (const std::exception& e) {
                return JsonRpcError{error::InternalError, e.what(), std::nullopt};
            }
        });

        // logging/setLevel
        router.on_request("logging/setLevel", [this](const nlohmann::json& params) -> HandlerResult {
            LogLevel level;
            from_json(params.at("level"), level);
            min_log_level = level;
            return nlohmann::json::object();
        });

        // notifications/cancelled
        router.on_notification("notifications/cancelled", [](const nlohmann::json& params) {
            // TODO: cancel the in-progress request
            (void)params;
        });

        // Handle incoming responses (server<-client responses to server->client requests)
        // These come in as JsonRpcResponse objects through the message handler
    }

    void on_message(JsonRpcMessage msg) {
        // Handle responses to our outbound requests
        if (auto* resp = std::get_if<JsonRpcResponse>(&msg)) {
            handle_response(*resp);
            return;
        }

        // Dispatch through router
        auto response = router.dispatch(msg);
        if (response) {
            send_message(*response);
        }
    }

    void handle_response(const JsonRpcResponse& resp) {
        std::lock_guard<std::mutex> lock(pending_mutex);
        // Convert ID to string key
        std::string key;
        if (auto* i = std::get_if<int64_t>(&resp.id)) {
            key = std::to_string(*i);
        } else if (auto* s = std::get_if<std::string>(&resp.id)) {
            key = *s;
        }
        auto it = pending_responses.find(key);
        if (it != pending_responses.end()) {
            it->second.set_value(resp);
            pending_responses.erase(it);
        }
    }

    JsonRpcResponse send_request_sync(const std::string& method, nlohmann::json params,
                                       std::chrono::milliseconds timeout) {
        int64_t id;
        std::future<JsonRpcResponse> fut;
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            id = next_outbound_id++;
            auto& p = pending_responses[std::to_string(id)];
            fut = p.get_future();
        }

        JsonRpcRequest req;
        req.id = RequestId{id};
        req.method = method;
        req.params = std::move(params);
        send_message(req);

        if (fut.wait_for(timeout) == std::future_status::timeout) {
            std::lock_guard<std::mutex> lock(pending_mutex);
            pending_responses.erase(std::to_string(id));
            throw McpTimeoutError("Request timed out: " + method);
        }
        return fut.get();
    }
};

// ----------- McpServer -----------

McpServer::McpServer(Options opts)
    : impl_(std::make_unique<Impl>(std::move(opts))) {
    impl_->tools.page_size = impl_->opts.page_size;
    impl_->resources.page_size = impl_->opts.page_size;
    impl_->resource_templates.page_size = impl_->opts.page_size;
    impl_->prompts.page_size = impl_->opts.page_size;
    impl_->session.set_request_timeout(impl_->opts.request_timeout);
    impl_->setup_handlers();
}

McpServer::~McpServer() {
    if (impl_) {
        impl_->stop_thread_pool();
    }
}

void McpServer::add_tool(ToolDefinition def, ToolHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->store_mutex);
    // Remove existing tool with same name
    auto& items = impl_->tools.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&def](const ToolDefinition& t) { return t.name == def.name; }), items.end());
    impl_->tool_handlers[def.name] = std::move(handler);
    items.push_back(std::move(def));

    // Notify if running
    if (impl_->running) {
        impl_->send_notification("notifications/tools/list_changed");
    }
}

void McpServer::add_tool_async(ToolDefinition def, AsyncToolHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->store_mutex);
    auto& items = impl_->tools.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&def](const ToolDefinition& t) { return t.name == def.name; }), items.end());
    impl_->async_tool_handlers[def.name] = std::move(handler);
    items.push_back(std::move(def));

    if (impl_->running) {
        impl_->send_notification("notifications/tools/list_changed");
    }
}

void McpServer::remove_tool(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->store_mutex);
    auto& items = impl_->tools.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&name](const ToolDefinition& t) { return t.name == name; }), items.end());
    impl_->tool_handlers.erase(name);
    impl_->async_tool_handlers.erase(name);

    if (impl_->running) {
        impl_->send_notification("notifications/tools/list_changed");
    }
}

void McpServer::add_resource(ResourceDefinition def, ResourceReadHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->store_mutex);
    auto& items = impl_->resources.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&def](const ResourceDefinition& r) { return r.uri == def.uri; }), items.end());
    impl_->resource_handlers[def.uri] = std::move(handler);
    items.push_back(std::move(def));

    if (impl_->running) {
        impl_->send_notification("notifications/resources/list_changed");
    }
}

void McpServer::add_resource_template(ResourceTemplate tmpl, ResourceReadHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->store_mutex);
    auto& items = impl_->resource_templates.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&tmpl](const ResourceTemplate& t) { return t.uri_template == tmpl.uri_template; }), items.end());
    impl_->resource_template_handlers[tmpl.uri_template] = std::move(handler);
    items.push_back(std::move(tmpl));
}

void McpServer::notify_resource_updated(const std::string& uri) {
    bool subscribed = false;
    {
        std::lock_guard<std::mutex> lock(impl_->store_mutex);
        subscribed = impl_->subscribed_uris.count(uri) > 0;
    }
    if (subscribed && impl_->running) {
        impl_->send_notification("notifications/resources/updated",
            nlohmann::json{{"uri", uri}});
    }
}

void McpServer::remove_resource(const std::string& uri) {
    std::lock_guard<std::mutex> lock(impl_->store_mutex);
    auto& items = impl_->resources.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&uri](const ResourceDefinition& r) { return r.uri == uri; }), items.end());
    impl_->resource_handlers.erase(uri);

    if (impl_->running) {
        impl_->send_notification("notifications/resources/list_changed");
    }
}

void McpServer::add_prompt(PromptDefinition def, PromptGetHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->store_mutex);
    auto& items = impl_->prompts.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&def](const PromptDefinition& p) { return p.name == def.name; }), items.end());
    impl_->prompt_handlers[def.name] = std::move(handler);
    items.push_back(std::move(def));

    if (impl_->running) {
        impl_->send_notification("notifications/prompts/list_changed");
    }
}

void McpServer::remove_prompt(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->store_mutex);
    auto& items = impl_->prompts.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&name](const PromptDefinition& p) { return p.name == name; }), items.end());
    impl_->prompt_handlers.erase(name);

    if (impl_->running) {
        impl_->send_notification("notifications/prompts/list_changed");
    }
}

void McpServer::set_completion_handler(CompletionHandler handler) {
    impl_->completion_handler = std::move(handler);
}

void McpServer::log(LogLevel level, const std::string& logger, const nlohmann::json& data) {
    if (level < impl_->min_log_level.load()) return;
    if (!impl_->running) return;

    nlohmann::json params = {
        {"level", level},
        {"logger", logger},
        {"data", data}
    };
    impl_->send_notification("notifications/message", params);
}

void McpServer::send_progress(const std::variant<int64_t, std::string>& token,
                               double progress,
                               std::optional<double> total,
                               std::optional<std::string> message) {
    nlohmann::json params;
    std::visit([&params](const auto& v) { params["progressToken"] = v; }, token);
    params["progress"] = progress;
    if (total) params["total"] = *total;
    if (message) params["message"] = *message;
    impl_->send_notification("notifications/progress", params);
}

SamplingResult McpServer::request_sampling(const SamplingRequest& req) {
    nlohmann::json params;
    to_json(params, req);

    auto resp = impl_->send_request_sync("sampling/createMessage", params, impl_->opts.request_timeout);
    if (resp.error) {
        throw McpProtocolError(resp.error->code, resp.error->message);
    }
    SamplingResult result;
    from_json(*resp.result, result);
    return result;
}

std::future<SamplingResult> McpServer::request_sampling_async(const SamplingRequest& req) {
    return std::async(std::launch::async, [this, req]() {
        return request_sampling(req);
    });
}

ElicitationResult McpServer::request_elicitation(const ElicitationRequest& req) {
    nlohmann::json params;
    to_json(params, req);

    auto resp = impl_->send_request_sync("elicitation/create", params, impl_->opts.request_timeout);
    if (resp.error) {
        throw McpProtocolError(resp.error->code, resp.error->message);
    }
    ElicitationResult result;
    from_json(*resp.result, result);
    return result;
}

std::vector<Root> McpServer::request_roots() {
    auto resp = impl_->send_request_sync("roots/list", nlohmann::json::object(), impl_->opts.request_timeout);
    if (resp.error) {
        throw McpProtocolError(resp.error->code, resp.error->message);
    }
    std::vector<Root> roots;
    if (resp.result && resp.result->contains("roots")) {
        roots = resp.result->at("roots").get<std::vector<Root>>();
    }
    return roots;
}

void McpServer::serve(std::unique_ptr<ITransport> transport) {
    impl_->running = true;
    impl_->start_thread_pool();

    auto* t = transport.get();
    impl_->transport = t;

    t->start([this](JsonRpcMessage msg) {
        impl_->on_message(std::move(msg));
    });

    impl_->running = false;
    impl_->transport = nullptr;
    impl_->stop_thread_pool();
}

void McpServer::serve_stdio() {
    serve(std::make_unique<StdioTransport>());
}

void McpServer::serve_http(const std::string& host, uint16_t port) {
    HttpServerTransport::Options opts;
    opts.host = host;
    opts.port = port;
    serve(std::make_unique<HttpServerTransport>(opts));
}

void McpServer::shutdown() {
    impl_->running = false;
    std::lock_guard<std::mutex> lock(impl_->transport_mutex);
    if (impl_->transport) {
        impl_->transport->shutdown();
    }
}

bool McpServer::is_running() const {
    return impl_->running;
}

} // namespace mcp
