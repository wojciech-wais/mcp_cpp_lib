#pragma once
#include <string>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

namespace mcp {

using RequestId = std::variant<int64_t, std::string>;

// Helper to convert RequestId to json
inline void to_json(nlohmann::json& j, const RequestId& id) {
    std::visit([&j](const auto& v) { j = v; }, id);
}

inline void from_json(const nlohmann::json& j, RequestId& id) {
    if (j.is_number_integer()) {
        id = j.get<int64_t>();
    } else if (j.is_string()) {
        id = j.get<std::string>();
    } else {
        throw std::invalid_argument("RequestId must be integer or string");
    }
}

struct JsonRpcError {
    int code;
    std::string message;
    std::optional<nlohmann::json> data;

    bool operator==(const JsonRpcError& o) const {
        return code == o.code && message == o.message && data == o.data;
    }
};

inline void to_json(nlohmann::json& j, const JsonRpcError& e) {
    j = nlohmann::json{{"code", e.code}, {"message", e.message}};
    if (e.data) j["data"] = *e.data;
}

inline void from_json(const nlohmann::json& j, JsonRpcError& e) {
    e.code = j.at("code").get<int>();
    e.message = j.at("message").get<std::string>();
    if (j.contains("data")) e.data = j.at("data");
}

struct JsonRpcRequest {
    RequestId id;
    std::string method;
    std::optional<nlohmann::json> params;
    // _meta field for progress tokens and other metadata
    std::optional<nlohmann::json> meta;

    bool operator==(const JsonRpcRequest& o) const {
        return id == o.id && method == o.method && params == o.params && meta == o.meta;
    }
};

struct JsonRpcResponse {
    RequestId id;
    std::optional<nlohmann::json> result;
    std::optional<JsonRpcError> error;

    bool operator==(const JsonRpcResponse& o) const {
        return id == o.id && result == o.result && error == o.error;
    }
};

struct JsonRpcNotification {
    std::string method;
    std::optional<nlohmann::json> params;

    bool operator==(const JsonRpcNotification& o) const {
        return method == o.method && params == o.params;
    }
};

using JsonRpcMessage = std::variant<JsonRpcRequest, JsonRpcResponse, JsonRpcNotification>;

void to_json(nlohmann::json& j, const JsonRpcRequest& r);
void from_json(const nlohmann::json& j, JsonRpcRequest& r);

void to_json(nlohmann::json& j, const JsonRpcResponse& r);
void from_json(const nlohmann::json& j, JsonRpcResponse& r);

void to_json(nlohmann::json& j, const JsonRpcNotification& n);
void from_json(const nlohmann::json& j, JsonRpcNotification& n);

void to_json(nlohmann::json& j, const JsonRpcMessage& m);

} // namespace mcp
