#include "mcp/transport/http_transport.hpp"
#include "mcp/error.hpp"
#include "mcp/version.hpp"

#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#include <httplib.h>

#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

namespace mcp {

// ---------- HttpServerTransport ----------

static std::string generate_uuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    uint64_t a = dis(gen), b = dis(gen);
    // Format as UUID v4
    a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8)  << (a >> 32);
    oss << "-" << std::setw(4) << ((a >> 16) & 0xFFFF);
    oss << "-" << std::setw(4) << (a & 0xFFFF);
    oss << "-" << std::setw(4) << (b >> 48);
    oss << "-" << std::setw(12) << (b & 0xFFFFFFFFFFFFull);
    return oss.str();
}

HttpServerTransport::HttpServerTransport(Options opts)
    : opts_(std::move(opts))
    , server_(std::make_unique<httplib::Server>()) {
}

HttpServerTransport::~HttpServerTransport() {
    shutdown();
}

bool HttpServerTransport::validate_origin(const std::string& origin) const {
    if (opts_.allowed_origins.empty()) return true;
    for (const auto& allowed : opts_.allowed_origins) {
        if (origin == allowed) return true;
    }
    return false;
}

void HttpServerTransport::setup_routes() {
    const std::string path = opts_.mcp_path;

    // POST: handle JSON-RPC requests
    server_->Post(path, [this](const httplib::Request& req, httplib::Response& res) {
        // Validate Origin header for DNS rebinding protection
        auto origin = req.get_header_value("Origin");
        if (!origin.empty() && !validate_origin(origin)) {
            res.status = 403;
            res.set_content("{\"error\":\"Invalid origin\"}", "application/json");
            return;
        }

        // Validate MCP-Protocol-Version header
        auto proto_ver = req.get_header_value("MCP-Protocol-Version");
        if (!proto_ver.empty() && proto_ver != std::string(PROTOCOL_VERSION)) {
            res.status = 400;
            res.set_content("{\"error\":\"Unsupported protocol version\"}", "application/json");
            return;
        }

        // Get or create session
        std::string session_id = req.get_header_value("Mcp-Session-Id");
        std::shared_ptr<HttpSession> session;

        if (session_id.empty()) {
            // New session
            session_id = generate_uuid();
            session = std::make_shared<HttpSession>();
            session->id = session_id;
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_[session_id] = session;
            }
            res.set_header("Mcp-Session-Id", session_id);
        } else {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                res.status = 404;
                res.set_content("{\"error\":\"Session not found\"}", "application/json");
                return;
            }
            session = it->second;
        }

        // Check if client accepts SSE
        auto accept = req.get_header_value("Accept");
        bool want_sse = accept.find("text/event-stream") != std::string::npos;

        try {
            // Check for batch
            bool is_batch = !req.body.empty() && req.body[0] == '[';

            if (want_sse) {
                // Return SSE stream for this request
                res.set_chunked_content_provider("text/event-stream",
                    [this, body = req.body, is_batch, session_id](size_t /*offset*/,
                         httplib::DataSink& sink) -> bool {
                        try {
                            auto process_msg = [&](const JsonRpcMessage& msg) {
                                nlohmann::json j;
                                to_json(j, msg);
                                std::string event = "data: " + j.dump() + "\n\n";
                                if (!sink.write(event.data(), event.size())) return false;
                                return true;
                            };

                            if (is_batch) {
                                auto msgs = Codec::parse_batch(body);
                                for (auto& m : msgs) {
                                    if (message_callback_) message_callback_(m);
                                }
                            } else {
                                auto msg = Codec::parse(body);
                                if (message_callback_) message_callback_(msg);
                            }
                            // Send done event
                            std::string done = "event: done\ndata: {}\n\n";
                            sink.write(done.data(), done.size());
                            sink.done();
                            return true;
                        } catch (...) {
                            return false;
                        }
                    });
            } else {
                // Return JSON response(s) synchronously
                // For simplicity, collect responses
                std::vector<nlohmann::json> responses;

                auto handle_one = [&](const JsonRpcMessage& msg) {
                    if (message_callback_) message_callback_(msg);
                };

                if (is_batch) {
                    auto msgs = Codec::parse_batch(req.body);
                    for (auto& m : msgs) handle_one(m);
                } else {
                    auto msg = Codec::parse(req.body);
                    handle_one(msg);
                }

                res.set_content("{}", "application/json");
            }
        } catch (const McpParseError& e) {
            res.status = 400;
            nlohmann::json err = {
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {{"code", error::ParseError}, {"message", e.what()}}}
            };
            res.set_content(err.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content("{\"error\":\"Internal server error\"}", "application/json");
        }
    });

    // GET: open SSE stream for server-initiated messages
    server_->Get(path, [this](const httplib::Request& req, httplib::Response& res) {
        auto origin = req.get_header_value("Origin");
        if (!origin.empty() && !validate_origin(origin)) {
            res.status = 403;
            return;
        }

        std::string session_id = req.get_header_value("Mcp-Session-Id");
        std::shared_ptr<HttpSession> session;

        if (session_id.empty()) {
            session_id = generate_uuid();
            session = std::make_shared<HttpSession>();
            session->id = session_id;
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_[session_id] = session;
            }
            res.set_header("Mcp-Session-Id", session_id);
        } else {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                res.status = 404;
                return;
            }
            session = it->second;
        }

        // Keep SSE stream open
        res.set_chunked_content_provider("text/event-stream",
            [session](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                // Set SSE sender for this session
                {
                    std::lock_guard<std::mutex> lock(session->mutex);
                    session->sse_sender = [&sink](const std::string& data) -> bool {
                        return sink.write(data.data(), data.size());
                    };
                }
                // Keep alive with comments every 30s
                std::string ping = ": ping\n\n";
                if (!sink.write(ping.data(), ping.size())) return false;
                // This provider returns false when the client disconnects
                return true;
            });
    });

    // DELETE: session termination
    server_->Delete(path, [this](const httplib::Request& req, httplib::Response& res) {
        std::string session_id = req.get_header_value("Mcp-Session-Id");
        if (session_id.empty()) {
            res.status = 400;
            return;
        }
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            res.status = 404;
            return;
        }
        sessions_.erase(it);
        res.status = 200;
    });
}

void HttpServerTransport::start(MessageCallback on_message, ErrorCallback on_error) {
    if (running_.exchange(true)) return;

    message_callback_ = std::move(on_message);
    error_callback_ = std::move(on_error);

    setup_routes();

    // Run server in blocking mode
    if (!server_->listen(opts_.host, opts_.port)) {
        running_ = false;
        throw McpTransportError("Failed to start HTTP server on " + opts_.host + ":" + std::to_string(opts_.port));
    }
}

void HttpServerTransport::send(const JsonRpcMessage& msg) {
    // Broadcast to all connected SSE clients
    nlohmann::json j;
    to_json(j, msg);
    std::string event = "data: " + j.dump() + "\n\n";

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        std::lock_guard<std::mutex> slock(session->mutex);
        if (session->sse_sender) {
            session->sse_sender(event);
        }
    }
}

void HttpServerTransport::send_to_session(const std::string& session_id, const JsonRpcMessage& msg) {
    nlohmann::json j;
    to_json(j, msg);
    std::string event = "data: " + j.dump() + "\n\n";

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        std::lock_guard<std::mutex> slock(it->second->mutex);
        if (it->second->sse_sender) {
            it->second->sse_sender(event);
        }
    }
}

void HttpServerTransport::shutdown() {
    if (!running_.exchange(false)) return;
    server_->stop();
}

bool HttpServerTransport::is_connected() const {
    return running_;
}

// ---------- HttpClientTransport ----------

HttpClientTransport::HttpClientTransport(const std::string& base_url)
    : base_url_(base_url) {
    // Parse host from URL
    // Support http://host:port/path
    std::string url = base_url;
    if (url.substr(0, 7) == "http://") url = url.substr(7);
    else if (url.substr(0, 8) == "https://") url = url.substr(8);

    // Extract host:port
    auto slash = url.find('/');
    std::string hostport = (slash == std::string::npos) ? url : url.substr(0, slash);

    client_ = std::make_unique<httplib::Client>(("http://" + hostport).c_str());
    client_->set_connection_timeout(10);
    client_->set_read_timeout(60);
}

HttpClientTransport::~HttpClientTransport() {
    shutdown();
    if (sse_thread_.joinable()) sse_thread_.join();
}

void HttpClientTransport::start(MessageCallback on_message, ErrorCallback on_error) {
    if (running_.exchange(true)) return;
    message_callback_ = std::move(on_message);
    error_callback_ = std::move(on_error);
    connected_ = true;
}

void HttpClientTransport::send(const JsonRpcMessage& msg) {
    if (!connected_) {
        throw McpTransportError("Not connected");
    }

    std::string body = Codec::serialize(msg);

    // Extract path from base_url
    std::string url = base_url_;
    if (url.substr(0, 7) == "http://") url = url.substr(7);
    else if (url.substr(0, 8) == "https://") url = url.substr(8);
    auto slash = url.find('/');
    std::string path = (slash == std::string::npos) ? "/" : url.substr(slash);

    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Accept", "application/json, text/event-stream"},
        {"MCP-Protocol-Version", std::string(PROTOCOL_VERSION)}
    };
    if (!session_id_.empty()) {
        headers.emplace("Mcp-Session-Id", session_id_);
    }

    auto result = client_->Post(path, headers, body, "application/json");
    if (!result) {
        throw McpTransportError("HTTP POST failed: " + httplib::to_string(result.error()));
    }

    // Store session ID if returned
    if (result->has_header("Mcp-Session-Id")) {
        session_id_ = result->get_header_value("Mcp-Session-Id");
    }

    if (result->status >= 400) {
        throw McpTransportError("HTTP error: " + std::to_string(result->status));
    }

    // Parse response if it's JSON
    if (!result->body.empty()) {
        try {
            auto resp_msg = Codec::parse(result->body);
            if (message_callback_) message_callback_(std::move(resp_msg));
        } catch (...) {
            // Might be SSE or empty
        }
    }
}

void HttpClientTransport::shutdown() {
    if (!running_.exchange(false)) return;
    connected_ = false;
}

bool HttpClientTransport::is_connected() const {
    return connected_;
}

} // namespace mcp
