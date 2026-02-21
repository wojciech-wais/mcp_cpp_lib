#include "mcp/json_rpc.hpp"
#include "mcp/version.hpp"

namespace mcp {

void to_json(nlohmann::json& j, const JsonRpcRequest& r) {
    nlohmann::json id_j;
    to_json(id_j, r.id);
    j = nlohmann::json::object();
    j["jsonrpc"] = std::string(JSONRPC_VERSION);
    j["id"] = id_j;
    j["method"] = r.method;
    if (r.params) j["params"] = *r.params;
    if (r.meta) j["_meta"] = *r.meta;
}

void from_json(const nlohmann::json& j, JsonRpcRequest& r) {
    from_json(j.at("id"), r.id);
    r.method = j.at("method").get<std::string>();
    if (j.contains("params")) r.params = j.at("params");
    if (j.contains("_meta")) r.meta = j.at("_meta");
}

void to_json(nlohmann::json& j, const JsonRpcResponse& r) {
    nlohmann::json id_j;
    to_json(id_j, r.id);
    j = nlohmann::json::object();
    j["jsonrpc"] = std::string(JSONRPC_VERSION);
    j["id"] = id_j;
    if (r.result) j["result"] = *r.result;
    if (r.error) j["error"] = *r.error;
}

void from_json(const nlohmann::json& j, JsonRpcResponse& r) {
    from_json(j.at("id"), r.id);
    if (j.contains("result")) r.result = j.at("result");
    if (j.contains("error")) r.error = j.at("error").get<JsonRpcError>();
}

void to_json(nlohmann::json& j, const JsonRpcNotification& n) {
    j = nlohmann::json::object();
    j["jsonrpc"] = std::string(JSONRPC_VERSION);
    j["method"] = n.method;
    if (n.params) j["params"] = *n.params;
}

void from_json(const nlohmann::json& j, JsonRpcNotification& n) {
    n.method = j.at("method").get<std::string>();
    if (j.contains("params")) n.params = j.at("params");
}

void to_json(nlohmann::json& j, const JsonRpcMessage& m) {
    std::visit([&j](const auto& v) { to_json(j, v); }, m);
}

} // namespace mcp
