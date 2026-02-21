#include "mcp/router.hpp"
#include "mcp/error.hpp"
#include "mcp/version.hpp"
#include <stdexcept>

namespace mcp {

void Router::on_request(const std::string& method, RequestHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    request_handlers_[method] = std::move(handler);
}

void Router::on_notification(const std::string& method, NotificationHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    notification_handlers_[method] = std::move(handler);
}

void Router::require_capability(const std::string& method, const std::string& capability) {
    std::lock_guard<std::mutex> lock(mutex_);
    capability_requirements_[method] = capability;
}

void Router::set_capabilities(const ServerCapabilities& server_caps,
                              const ClientCapabilities& client_caps) {
    std::lock_guard<std::mutex> lock(mutex_);
    server_caps_ = server_caps;
    client_caps_ = client_caps;
}

bool Router::has_handler(const std::string& method) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return request_handlers_.count(method) > 0 || notification_handlers_.count(method) > 0;
}

bool Router::check_capability(const std::string& method) const {
    auto req_it = capability_requirements_.find(method);
    if (req_it == capability_requirements_.end()) return true;

    const std::string& cap = req_it->second;

    // Check server capabilities
    if (cap == "tools" && server_caps_.tools) return true;
    if (cap == "resources" && server_caps_.resources) return true;
    if (cap == "prompts" && server_caps_.prompts) return true;
    if (cap == "logging" && server_caps_.logging) return true;
    if (cap == "completions" && server_caps_.completions) return true;
    // Check client capabilities
    if (cap == "sampling" && client_caps_.sampling) return true;
    if (cap == "roots" && client_caps_.roots) return true;
    if (cap == "elicitation" && client_caps_.elicitation) return true;

    return false;
}

std::optional<JsonRpcMessage> Router::dispatch(const JsonRpcMessage& msg,
                                                const Session* /*session*/) {
    if (const auto* req = std::get_if<JsonRpcRequest>(&msg)) {
        // Hold lock only to look up handler and check capability
        RequestHandler handler;
        RequestId req_id = req->id;
        std::string method = req->method;
        nlohmann::json params = req->params ? *req->params : nlohmann::json::object();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!check_capability(method)) {
                JsonRpcResponse resp;
                resp.id = req_id;
                resp.error = JsonRpcError{
                    error::InvalidRequest,
                    "Capability not supported: " + method,
                    std::nullopt
                };
                return resp;
            }

            auto it = request_handlers_.find(method);
            if (it == request_handlers_.end()) {
                JsonRpcResponse resp;
                resp.id = req_id;
                resp.error = JsonRpcError{
                    error::MethodNotFound,
                    "Method not found: " + method,
                    std::nullopt
                };
                return resp;
            }
            handler = it->second;
        }
        // Call handler WITHOUT holding the lock to prevent deadlock
        // when handlers call back into router methods (e.g. set_capabilities)
        try {
            auto result = handler(params);

            JsonRpcResponse resp;
            resp.id = req_id;
            if (auto* ok = std::get_if<nlohmann::json>(&result)) {
                resp.result = *ok;
            } else if (auto* err = std::get_if<JsonRpcError>(&result)) {
                resp.error = *err;
            }
            return resp;
        } catch (const McpProtocolError& e) {
            JsonRpcResponse resp;
            resp.id = req_id;
            resp.error = JsonRpcError{e.code, e.what(), std::nullopt};
            return resp;
        } catch (const std::exception& e) {
            JsonRpcResponse resp;
            resp.id = req_id;
            resp.error = JsonRpcError{error::InternalError, e.what(), std::nullopt};
            return resp;
        }
    } else if (const auto* notif = std::get_if<JsonRpcNotification>(&msg)) {
        // Hold lock only to look up handler
        NotificationHandler handler;
        nlohmann::json params = notif->params ? *notif->params : nlohmann::json::object();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = notification_handlers_.find(notif->method);
            if (it == notification_handlers_.end()) return std::nullopt;
            handler = it->second;
        }
        // Call handler WITHOUT holding the lock
        try {
            handler(params);
        } catch (...) {
            // Notifications don't return responses
        }
        return std::nullopt;
    } else if (const auto* resp = std::get_if<JsonRpcResponse>(&msg)) {
        // Responses are not dispatched through the router (handled by session)
        (void)resp;
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace mcp
