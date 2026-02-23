#pragma once
#include "../json_rpc.hpp"
#include <functional>

namespace mcp {

/// Callback for incoming messages
using MessageCallback = std::function<void(JsonRpcMessage)>;
using ErrorCallback = std::function<void(std::exception_ptr)>;

/// Abstract transport interface
class ITransport {
public:
    virtual ~ITransport() = default;

    /// Start the transport. Blocks until shutdown (or runs event loop).
    virtual void start(MessageCallback on_message,
                       ErrorCallback on_error = nullptr) = 0;

    /// Send a message to the remote peer.
    virtual void send(const JsonRpcMessage& msg) = 0;

    /// Graceful shutdown.
    virtual void shutdown() = 0;

    /// Check if transport is connected.
    [[nodiscard]] virtual bool is_connected() const = 0;
};

} // namespace mcp
