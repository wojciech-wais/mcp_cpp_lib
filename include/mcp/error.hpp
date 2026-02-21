#pragma once
#include <stdexcept>
#include <string>

namespace mcp {

class McpError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class McpParseError : public McpError {
public:
    using McpError::McpError;
};

class McpProtocolError : public McpError {
public:
    int code;
    McpProtocolError(int code, const std::string& msg)
        : McpError(msg), code(code) {}
};

class McpTransportError : public McpError {
public:
    using McpError::McpError;
};

class McpTimeoutError : public McpError {
public:
    using McpError::McpError;
};

namespace error {
    constexpr int ParseError       = -32700;
    constexpr int InvalidRequest   = -32600;
    constexpr int MethodNotFound   = -32601;
    constexpr int InvalidParams    = -32602;
    constexpr int InternalError    = -32603;
    constexpr int ResourceNotFound = -32002;
} // namespace error

} // namespace mcp
