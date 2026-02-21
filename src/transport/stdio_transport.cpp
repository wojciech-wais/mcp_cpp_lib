#include "mcp/transport/stdio_transport.hpp"
#include "mcp/error.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace mcp {

StdioTransport::StdioTransport()
    : read_fd_(STDIN_FILENO), write_fd_(STDOUT_FILENO), owns_fds_(false) {
}

StdioTransport::StdioTransport(int read_fd, int write_fd)
    : read_fd_(read_fd), write_fd_(write_fd), owns_fds_(true) {
}

StdioTransport::~StdioTransport() {
    shutdown();
    if (reader_thread_.joinable()) reader_thread_.join();
    if (writer_thread_.joinable()) writer_thread_.join();
    if (owns_fds_) {
        if (read_fd_ >= 0)  ::close(read_fd_);
        if (write_fd_ >= 0) ::close(write_fd_);
    }
    if (wakeup_pipe_[0] >= 0) ::close(wakeup_pipe_[0]);
    if (wakeup_pipe_[1] >= 0) ::close(wakeup_pipe_[1]);
}

void StdioTransport::start(MessageCallback on_message, ErrorCallback on_error) {
    // If shutdown() was called before start(), don't block — exit immediately.
    if (shutdown_requested_.load()) return;
    if (running_.exchange(true)) {
        return; // already running
    }
    connected_ = true;

    // Create wakeup pipe for writer
    if (pipe(wakeup_pipe_) < 0) {
        throw McpTransportError("Failed to create wakeup pipe");
    }

    // Set non-blocking on write end of wakeup pipe
    int flags = fcntl(wakeup_pipe_[1], F_GETFL, 0);
    fcntl(wakeup_pipe_[1], F_SETFL, flags | O_NONBLOCK);

    writer_thread_ = std::thread([this]() { write_loop(); });
    read_loop(std::move(on_message), std::move(on_error));
}

void StdioTransport::read_loop(MessageCallback on_message, ErrorCallback on_error) {
    std::string buffer;
    buffer.reserve(4096);

    char chunk[4096];

    while (running_) {
        // Use poll() so that shutdown() can interrupt the blocking read
        // via the wakeup pipe.
        struct pollfd fds[2];
        fds[0].fd = read_fd_;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = wakeup_pipe_[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        int ret = ::poll(fds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Wakeup pipe has data → shutdown() was called, exit cleanly
        if (fds[1].revents & POLLIN) break;

        if (!(fds[0].revents & POLLIN)) continue;

        ssize_t n = ::read(read_fd_, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (!running_) break;
            if (on_error) {
                try {
                    throw McpTransportError(std::string("Read error: ") + strerror(errno));
                } catch (...) {
                    on_error(std::current_exception());
                }
            }
            break;
        }
        if (n == 0) {
            // EOF
            connected_ = false;
            running_ = false;
            // Wake up writer
            char b = 1;
            ::write(wakeup_pipe_[1], &b, 1);
            break;
        }

        buffer.append(chunk, static_cast<size_t>(n));

        // Process complete lines
        size_t pos = 0;
        while (true) {
            size_t nl = buffer.find('\n', pos);
            if (nl == std::string::npos) break;

            std::string line = buffer.substr(pos, nl - pos);
            pos = nl + 1;

            if (line.empty()) continue;

            // Remove trailing \r if present (CRLF)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) continue;

            try {
                auto msg = Codec::parse(line);
                on_message(std::move(msg));
            } catch (const std::exception& e) {
                if (on_error) {
                    try {
                        throw McpParseError(e.what());
                    } catch (...) {
                        on_error(std::current_exception());
                    }
                }
            }
        }

        if (pos > 0) {
            buffer = buffer.substr(pos);
        }
    }
}

void StdioTransport::write_loop() {
    while (true) {
        std::string msg_to_write;
        {
            std::unique_lock<std::mutex> lock(write_mutex_);
            write_cv_.wait(lock, [this] {
                return !write_queue_.empty() || !running_;
            });

            if (!running_ && write_queue_.empty()) break;
            if (!write_queue_.empty()) {
                msg_to_write = std::move(write_queue_.front());
                write_queue_.pop();
            }
        }

        if (!msg_to_write.empty()) {
            msg_to_write += '\n';
            const char* data = msg_to_write.data();
            size_t remaining = msg_to_write.size();

            while (remaining > 0) {
                ssize_t written = ::write(write_fd_, data, remaining);
                if (written < 0) {
                    if (errno == EINTR) continue;
                    break; // Error - stop writing
                }
                data += written;
                remaining -= static_cast<size_t>(written);
            }
        }
    }
}

void StdioTransport::send(const JsonRpcMessage& msg) {
    // Throw only if permanently shut down, not if start() hasn't run yet.
    // Messages queued before start() will be drained once write_loop() starts.
    if (shutdown_requested_.load()) {
        throw McpTransportError("Transport shut down");
    }
    std::string serialized = Codec::serialize(msg);
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_queue_.push(std::move(serialized));
    }
    write_cv_.notify_one();
}

void StdioTransport::shutdown() {
    shutdown_requested_ = true;
    if (!running_.exchange(false)) {
        // start() hasn't been called yet (or already shut down).
        // Notify write_cv_ in case write_loop is waiting, so it exits.
        write_cv_.notify_all();
        return;
    }
    connected_ = false;
    write_cv_.notify_all();
    // Write to wakeup pipe to interrupt poll() in read_loop().
    if (wakeup_pipe_[1] >= 0) {
        char b = 1;
        ::write(wakeup_pipe_[1], &b, 1);
    }
}

bool StdioTransport::is_connected() const {
    return connected_;
}

} // namespace mcp
