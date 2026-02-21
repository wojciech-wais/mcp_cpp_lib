#pragma once
#include "transport.hpp"
#include "../codec.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace mcp {

/// StdioTransport reads newline-delimited JSON from stdin and writes to stdout.
/// Uses a background reader thread and a write queue.
class StdioTransport : public ITransport {
public:
    /// Create transport using system stdin/stdout.
    StdioTransport();

    /// Create transport using specified file descriptors (for testing).
    StdioTransport(int read_fd, int write_fd);

    ~StdioTransport() override;

    void start(MessageCallback on_message, ErrorCallback on_error = nullptr) override;
    void send(const JsonRpcMessage& msg) override;
    void shutdown() override;
    bool is_connected() const override;

private:
    void read_loop(MessageCallback on_message, ErrorCallback on_error);
    void write_loop();

    int read_fd_;
    int write_fd_;
    bool owns_fds_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> shutdown_requested_{false};

    std::thread reader_thread_;
    std::thread writer_thread_;

    std::mutex write_mutex_;
    std::queue<std::string> write_queue_;
    std::condition_variable write_cv_;

    int wakeup_pipe_[2]{-1, -1};  // pipe for waking up writer
};

} // namespace mcp
