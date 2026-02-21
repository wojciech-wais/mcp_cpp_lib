#pragma once
#include "transport.hpp"
#include "../codec.hpp"
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <thread>
#include <functional>
#include <vector>

// Forward declarations to avoid including heavy httplib header
namespace httplib {
    class Server;
    class Client;
    class Response;
}

namespace mcp {

/// Session data for HTTP transport
struct HttpSession {
    std::string id;
    std::mutex mutex;
    // SSE sink for server-initiated messages (if GET SSE stream is open)
    std::function<bool(const std::string& data)> sse_sender;
};

/// HTTP server transport implementing Streamable HTTP MCP spec.
class HttpServerTransport : public ITransport {
public:
    struct Options {
        std::string host = "127.0.0.1";
        uint16_t port = 8080;
        std::string mcp_path = "/mcp";
        std::vector<std::string> allowed_origins;
        int max_connections = 100;
    };

    explicit HttpServerTransport(Options opts);
    ~HttpServerTransport() override;

    void start(MessageCallback on_message, ErrorCallback on_error = nullptr) override;
    void send(const JsonRpcMessage& msg) override;
    void shutdown() override;
    bool is_connected() const override;

    /// Send a message to a specific session (for server-initiated messages).
    void send_to_session(const std::string& session_id, const JsonRpcMessage& msg);

    uint16_t port() const { return opts_.port; }

private:
    std::string generate_session_id();
    bool validate_origin(const std::string& origin) const;
    void setup_routes();

    Options opts_;
    std::unique_ptr<httplib::Server> server_;
    std::atomic<bool> running_{false};

    std::mutex sessions_mutex_;
    std::map<std::string, std::shared_ptr<HttpSession>> sessions_;

    MessageCallback message_callback_;
    ErrorCallback error_callback_;

    // For broadcasting to all connected SSE clients
    std::mutex broadcast_mutex_;
};

/// HTTP client transport for connecting to an MCP server.
class HttpClientTransport : public ITransport {
public:
    explicit HttpClientTransport(const std::string& base_url);
    ~HttpClientTransport() override;

    void start(MessageCallback on_message, ErrorCallback on_error = nullptr) override;
    void send(const JsonRpcMessage& msg) override;
    void shutdown() override;
    bool is_connected() const override;

private:
    std::string base_url_;
    std::string session_id_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::unique_ptr<httplib::Client> client_;
    std::thread sse_thread_;
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
};

} // namespace mcp
