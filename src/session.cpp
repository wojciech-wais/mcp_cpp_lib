#include "mcp/session.hpp"
#include "mcp/error.hpp"
#include <algorithm>
#include <sstream>

namespace mcp {

Session::Session() = default;

SessionState Session::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void Session::set_state(SessionState s) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = s;
}

RequestId Session::next_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    return RequestId{next_id_++};
}

RequestId Session::register_request(const std::string& method,
                                     std::function<void(const JsonRpcResponse&)> cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t id = next_id_++;
    PendingRequest req;
    req.method = method;
    req.created_at = std::chrono::steady_clock::now();
    req.callback = std::move(cb);
    pending_requests_int_[id] = std::move(req);
    return RequestId{id};
}

bool Session::complete_request(const RequestId& id, const JsonRpcResponse& resp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* int_id = std::get_if<int64_t>(&id)) {
        auto it = pending_requests_int_.find(*int_id);
        if (it == pending_requests_int_.end()) return false;
        if (it->second.callback) it->second.callback(resp);
        pending_requests_int_.erase(it);
        return true;
    } else if (auto* str_id = std::get_if<std::string>(&id)) {
        auto it = pending_requests_str_.find(*str_id);
        if (it == pending_requests_str_.end()) return false;
        if (it->second.callback) it->second.callback(resp);
        pending_requests_str_.erase(it);
        return true;
    }
    return false;
}

void Session::register_progress_token(const RequestId& request_id,
                                       const std::variant<int64_t, std::string>& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* int_id = std::get_if<int64_t>(&request_id)) {
        auto it = pending_requests_int_.find(*int_id);
        if (it != pending_requests_int_.end()) {
            it->second.progress_token = token;
        }
    } else if (auto* str_id = std::get_if<std::string>(&request_id)) {
        auto it = pending_requests_str_.find(*str_id);
        if (it != pending_requests_str_.end()) {
            it->second.progress_token = token;
        }
    }
}

void Session::set_request_timeout(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(mutex_);
    request_timeout_ = timeout;
}

std::vector<RequestId> Session::check_timeouts() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    std::vector<RequestId> timed_out;

    for (auto it = pending_requests_int_.begin(); it != pending_requests_int_.end(); ) {
        if (now - it->second.created_at > request_timeout_) {
            timed_out.push_back(RequestId{it->first});
            it = pending_requests_int_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = pending_requests_str_.begin(); it != pending_requests_str_.end(); ) {
        if (now - it->second.created_at > request_timeout_) {
            timed_out.push_back(RequestId{it->first});
            it = pending_requests_str_.erase(it);
        } else {
            ++it;
        }
    }
    return timed_out;
}

ServerCapabilities& Session::server_capabilities() {
    std::lock_guard<std::mutex> lock(mutex_);
    return server_caps_;
}

const ServerCapabilities& Session::server_capabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return server_caps_;
}

ClientCapabilities& Session::client_capabilities() {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_caps_;
}

const ClientCapabilities& Session::client_capabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_caps_;
}

std::string& Session::protocol_version() {
    std::lock_guard<std::mutex> lock(mutex_);
    return protocol_version_;
}

const std::string& Session::protocol_version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return protocol_version_;
}

std::optional<std::string>& Session::session_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_id_;
}

const std::optional<std::string>& Session::session_id() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_id_;
}

bool Session::has_pending_request(const RequestId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* int_id = std::get_if<int64_t>(&id)) {
        return pending_requests_int_.count(*int_id) > 0;
    } else if (auto* str_id = std::get_if<std::string>(&id)) {
        return pending_requests_str_.count(*str_id) > 0;
    }
    return false;
}

} // namespace mcp
