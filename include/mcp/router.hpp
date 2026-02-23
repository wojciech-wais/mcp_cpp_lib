#pragma once
#include "types.hpp"
#include "json_rpc.hpp"
#include <functional>
#include <optional>
#include <unordered_map>
#include <string>
#include <mutex>

namespace mcp {

// Forward declaration
class Session;

using HandlerResult = std::variant<nlohmann::json, JsonRpcError>;
using RequestHandler = std::function<HandlerResult(const nlohmann::json& params)>;
using NotificationHandler = std::function<void(const nlohmann::json& params)>;

class Router {
public:
    /// Register a request handler for a method.
    void on_request(const std::string& method, RequestHandler handler);

    /// Register a notification handler.
    void on_notification(const std::string& method, NotificationHandler handler);

    /// Dispatch an incoming message. Returns response if applicable.
    [[nodiscard]] std::optional<JsonRpcMessage> dispatch(const JsonRpcMessage& msg,
                                            const Session* session = nullptr);

    /// Set required capability for a method (enforced during dispatch).
    void require_capability(const std::string& method, const std::string& capability);

    /// Provide current negotiated capabilities for gating.
    void set_capabilities(const ServerCapabilities& server_caps,
                          const ClientCapabilities& client_caps);

    [[nodiscard]] bool has_handler(const std::string& method) const;

private:
    bool check_capability(const std::string& method) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RequestHandler> request_handlers_;
    std::unordered_map<std::string, NotificationHandler> notification_handlers_;
    std::unordered_map<std::string, std::string> capability_requirements_;
    ServerCapabilities server_caps_;
    ClientCapabilities client_caps_;
};

} // namespace mcp
