#pragma once
#include "types.hpp"
#include "json_rpc.hpp"
#include <map>
#include <optional>
#include <chrono>
#include <functional>
#include <mutex>

namespace mcp {

enum class SessionState {
    Uninitialized,
    Initializing,
    Ready,
    ShuttingDown,
    Closed
};

struct PendingRequest {
    std::string method;
    std::chrono::steady_clock::time_point created_at;
    std::function<void(const JsonRpcResponse&)> callback;
    std::optional<std::variant<int64_t, std::string>> progress_token;
};

class Session {
public:
    Session();

    SessionState state() const;
    void set_state(SessionState s);

    /// Generate a new request ID.
    RequestId next_id();

    /// Register a pending outgoing request.
    RequestId register_request(const std::string& method,
                                std::function<void(const JsonRpcResponse&)> cb = nullptr);

    /// Handle response for a pending request.
    bool complete_request(const RequestId& id, const JsonRpcResponse& resp);

    /// Track progress tokens.
    void register_progress_token(const RequestId& request_id,
                                  const std::variant<int64_t, std::string>& token);

    /// Timeout management.
    void set_request_timeout(std::chrono::milliseconds timeout);
    std::vector<RequestId> check_timeouts();

    /// Negotiated capabilities.
    ServerCapabilities& server_capabilities();
    const ServerCapabilities& server_capabilities() const;
    ClientCapabilities& client_capabilities();
    const ClientCapabilities& client_capabilities() const;

    std::string& protocol_version();
    const std::string& protocol_version() const;

    std::optional<std::string>& session_id();
    const std::optional<std::string>& session_id() const;

    bool has_pending_request(const RequestId& id) const;

private:
    mutable std::mutex mutex_;
    SessionState state_{SessionState::Uninitialized};
    std::map<std::string, PendingRequest> pending_requests_str_;
    std::map<int64_t, PendingRequest> pending_requests_int_;
    ServerCapabilities server_caps_;
    ClientCapabilities client_caps_;
    std::string protocol_version_;
    std::optional<std::string> session_id_;
    int64_t next_id_{1};
    std::chrono::milliseconds request_timeout_{30000};
};

} // namespace mcp
