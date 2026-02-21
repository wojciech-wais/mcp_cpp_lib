#include "mcp/codec.hpp"
#include "mcp/error.hpp"
#include "mcp/version.hpp"
#include <nlohmann/json.hpp>
#include <simdjson.h>
#include <stdexcept>
#include <string>

namespace mcp {

namespace {

// Convert simdjson value to nlohmann::json recursively
nlohmann::json simdjson_to_nlohmann(simdjson::ondemand::value val) {
    switch (val.type()) {
        case simdjson::ondemand::json_type::object: {
            nlohmann::json obj = nlohmann::json::object();
            auto object = val.get_object();
            for (auto field : object) {
                std::string_view key = field.unescaped_key();
                obj[std::string(key)] = simdjson_to_nlohmann(field.value());
            }
            return obj;
        }
        case simdjson::ondemand::json_type::array: {
            nlohmann::json arr = nlohmann::json::array();
            for (auto elem : val.get_array()) {
                arr.push_back(simdjson_to_nlohmann(elem.value()));
            }
            return arr;
        }
        case simdjson::ondemand::json_type::string: {
            std::string_view sv = val.get_string();
            return nlohmann::json(std::string(sv));
        }
        case simdjson::ondemand::json_type::number: {
            // Try integer first, then double
            auto result_int = val.get_int64();
            if (result_int.error() == simdjson::SUCCESS) {
                return nlohmann::json(result_int.value());
            }
            auto result_uint = val.get_uint64();
            if (result_uint.error() == simdjson::SUCCESS) {
                return nlohmann::json(result_uint.value());
            }
            return nlohmann::json(val.get_double().value());
        }
        case simdjson::ondemand::json_type::boolean:
            return nlohmann::json(val.get_bool().value());
        case simdjson::ondemand::json_type::null:
            return nlohmann::json(nullptr);
        default:
            return nlohmann::json(nullptr);
    }
}

nlohmann::json simdjson_doc_to_nlohmann(simdjson::ondemand::document& doc) {
    auto val = doc.get_value();
    if (val.error()) {
        throw McpParseError("Failed to get document value");
    }
    return simdjson_to_nlohmann(val.value());
}

} // anonymous namespace

JsonRpcMessage Codec::parse_object(const nlohmann::json& j) {
    // Validate jsonrpc version
    if (!j.contains("jsonrpc")) {
        throw McpParseError("Missing 'jsonrpc' field");
    }
    if (j.at("jsonrpc").get<std::string>() != JSONRPC_VERSION) {
        throw McpParseError("Invalid jsonrpc version, expected '2.0'");
    }

    bool has_id = j.contains("id");
    bool has_method = j.contains("method");

    if (has_method && has_id) {
        // It's a request
        // Validate ID is not null
        if (j.at("id").is_null()) {
            throw McpParseError("Request ID must not be null");
        }
        JsonRpcRequest req;
        from_json(j.at("id"), req.id);
        req.method = j.at("method").get<std::string>();
        if (j.contains("params")) req.params = j.at("params");
        if (j.contains("_meta")) req.meta = j.at("_meta");
        return req;
    } else if (has_method && !has_id) {
        // It's a notification
        JsonRpcNotification notif;
        notif.method = j.at("method").get<std::string>();
        if (j.contains("params")) notif.params = j.at("params");
        return notif;
    } else if (has_id && !has_method) {
        // It's a response
        if (j.at("id").is_null()) {
            throw McpParseError("Response ID must not be null");
        }
        JsonRpcResponse resp;
        from_json(j.at("id"), resp.id);
        if (j.contains("result")) resp.result = j.at("result");
        if (j.contains("error")) resp.error = j.at("error").get<JsonRpcError>();
        return resp;
    } else {
        throw McpParseError("Cannot determine message type: missing both 'id' and 'method'");
    }
}

JsonRpcMessage Codec::parse(std::string_view raw) {
    if (raw.empty()) {
        throw McpParseError("Empty input");
    }

    // Use simdjson for fast parsing
    simdjson::ondemand::parser parser;
    // simdjson requires padded input
    simdjson::padded_string padded(raw.data(), raw.size());

    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);
    if (error) {
        throw McpParseError(std::string("JSON parse error: ") + simdjson::error_message(error));
    }

    // Convert to nlohmann for further processing
    nlohmann::json j;
    try {
        j = simdjson_doc_to_nlohmann(doc);
    } catch (const std::exception& e) {
        throw McpParseError(std::string("JSON conversion error: ") + e.what());
    }

    if (!j.is_object()) {
        throw McpParseError("Message must be a JSON object");
    }

    return parse_object(j);
}

std::vector<JsonRpcMessage> Codec::parse_batch(std::string_view raw) {
    if (raw.empty()) {
        throw McpParseError("Empty input");
    }

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(raw.data(), raw.size());

    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);
    if (error) {
        throw McpParseError(std::string("JSON parse error: ") + simdjson::error_message(error));
    }

    nlohmann::json j;
    try {
        j = simdjson_doc_to_nlohmann(doc);
    } catch (const std::exception& e) {
        throw McpParseError(std::string("JSON conversion error: ") + e.what());
    }

    if (!j.is_array()) {
        throw McpParseError("Batch must be a JSON array");
    }

    std::vector<JsonRpcMessage> messages;
    messages.reserve(j.size());
    for (const auto& item : j) {
        if (!item.is_object()) {
            throw McpParseError("Each batch item must be a JSON object");
        }
        messages.push_back(parse_object(item));
    }
    return messages;
}

std::string Codec::serialize(const JsonRpcMessage& msg) {
    nlohmann::json j;
    to_json(j, msg);
    return j.dump();
}

std::string Codec::serialize_batch(const std::vector<JsonRpcMessage>& msgs) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& msg : msgs) {
        nlohmann::json j;
        to_json(j, msg);
        arr.push_back(std::move(j));
    }
    return arr.dump();
}

} // namespace mcp
