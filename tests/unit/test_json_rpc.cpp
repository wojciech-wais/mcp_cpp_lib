#include <gtest/gtest.h>
#include "mcp/json_rpc.hpp"
#include "mcp/version.hpp"
#include <nlohmann/json.hpp>

using namespace mcp;

TEST(JsonRpcRequest, ConstructAndSerialize) {
    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "tools/list";
    req.params = nlohmann::json{{"cursor", "abc"}};

    nlohmann::json j;
    to_json(j, req);
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["method"], "tools/list");
    EXPECT_EQ(j["id"], 1);
    EXPECT_EQ(j["params"]["cursor"], "abc");
}

TEST(JsonRpcRequest, StringId) {
    JsonRpcRequest req;
    req.id = RequestId{std::string{"my-id"}};
    req.method = "ping";

    nlohmann::json j;
    to_json(j, req);
    EXPECT_EQ(j["id"], "my-id");
}

TEST(JsonRpcResponse, WithResult) {
    JsonRpcResponse resp;
    resp.id = RequestId{int64_t{42}};
    resp.result = nlohmann::json{{"ok", true}};

    nlohmann::json j;
    to_json(j, resp);
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], 42);
    EXPECT_EQ(j["result"]["ok"], true);
    EXPECT_FALSE(j.contains("error"));
}

TEST(JsonRpcResponse, WithError) {
    JsonRpcResponse resp;
    resp.id = RequestId{int64_t{1}};
    resp.error = JsonRpcError{-32601, "Method not found", std::nullopt};

    nlohmann::json j;
    to_json(j, resp);
    EXPECT_EQ(j["error"]["code"], -32601);
    EXPECT_EQ(j["error"]["message"], "Method not found");
}

TEST(JsonRpcNotification, Serialize) {
    JsonRpcNotification notif;
    notif.method = "notifications/initialized";

    nlohmann::json j;
    to_json(j, notif);
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["method"], "notifications/initialized");
    EXPECT_FALSE(j.contains("id"));
}

TEST(RequestId, IntId) {
    RequestId id = int64_t{123};
    nlohmann::json j;
    to_json(j, id);
    EXPECT_EQ(j, 123);
}

TEST(RequestId, StringId) {
    RequestId id = std::string{"hello"};
    nlohmann::json j;
    to_json(j, id);
    EXPECT_EQ(j, "hello");
}

TEST(RequestId, FromJsonInt) {
    nlohmann::json j = 42;
    RequestId id;
    from_json(j, id);
    ASSERT_TRUE(std::holds_alternative<int64_t>(id));
    EXPECT_EQ(std::get<int64_t>(id), 42);
}

TEST(RequestId, FromJsonString) {
    nlohmann::json j = "my-request";
    RequestId id;
    from_json(j, id);
    ASSERT_TRUE(std::holds_alternative<std::string>(id));
    EXPECT_EQ(std::get<std::string>(id), "my-request");
}

TEST(RequestId, FromJsonNull) {
    nlohmann::json j = nullptr;
    RequestId id;
    EXPECT_THROW(from_json(j, id), std::invalid_argument);
}

TEST(JsonRpcError, Equality) {
    JsonRpcError e1{-32601, "Not found", std::nullopt};
    JsonRpcError e2{-32601, "Not found", std::nullopt};
    JsonRpcError e3{-32600, "Invalid", std::nullopt};
    EXPECT_EQ(e1, e2);
    EXPECT_NE(e1, e3);
}
