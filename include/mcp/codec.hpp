#pragma once
#include "json_rpc.hpp"
#include "error.hpp"
#include <string_view>
#include <vector>

namespace mcp {

class Codec {
public:
    /// Parse raw JSON bytes into a message.
    /// Throws McpParseError on invalid JSON or missing required fields.
    static JsonRpcMessage parse(std::string_view raw);

    /// Parse a batch of messages (JSON array).
    static std::vector<JsonRpcMessage> parse_batch(std::string_view raw);

    /// Serialize a message to JSON string.
    static std::string serialize(const JsonRpcMessage& msg);

    /// Serialize a batch.
    static std::string serialize_batch(const std::vector<JsonRpcMessage>& msgs);

private:
    static JsonRpcMessage parse_object(const nlohmann::json& j);
};

} // namespace mcp
