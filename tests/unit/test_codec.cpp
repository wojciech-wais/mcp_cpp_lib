#include <gtest/gtest.h>
#include "mcp/codec.hpp"
#include "mcp/error.hpp"

using namespace mcp;

// ---- Parse tests ----

TEST(CodecParse, ValidRequest) {
    auto msg = Codec::parse(R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{}})");
    ASSERT_TRUE(std::holds_alternative<JsonRpcRequest>(msg));
    auto& req = std::get<JsonRpcRequest>(msg);
    EXPECT_EQ(std::get<int64_t>(req.id), 1);
    EXPECT_EQ(req.method, "ping");
}

TEST(CodecParse, ValidRequestStringId) {
    auto msg = Codec::parse(R"({"jsonrpc":"2.0","id":"abc-123","method":"tools/list"})");
    ASSERT_TRUE(std::holds_alternative<JsonRpcRequest>(msg));
    auto& req = std::get<JsonRpcRequest>(msg);
    EXPECT_EQ(std::get<std::string>(req.id), "abc-123");
    EXPECT_EQ(req.method, "tools/list");
}

TEST(CodecParse, ValidResponse) {
    auto msg = Codec::parse(R"({"jsonrpc":"2.0","id":42,"result":{"tools":[]}})");
    ASSERT_TRUE(std::holds_alternative<JsonRpcResponse>(msg));
    auto& resp = std::get<JsonRpcResponse>(msg);
    EXPECT_EQ(std::get<int64_t>(resp.id), 42);
    ASSERT_TRUE(resp.result.has_value());
    EXPECT_TRUE(resp.result->contains("tools"));
}

TEST(CodecParse, ValidErrorResponse) {
    auto msg = Codec::parse(R"({"jsonrpc":"2.0","id":1,"error":{"code":-32601,"message":"Method not found"}})");
    ASSERT_TRUE(std::holds_alternative<JsonRpcResponse>(msg));
    auto& resp = std::get<JsonRpcResponse>(msg);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, -32601);
    EXPECT_EQ(resp.error->message, "Method not found");
}

TEST(CodecParse, ValidNotification) {
    auto msg = Codec::parse(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    ASSERT_TRUE(std::holds_alternative<JsonRpcNotification>(msg));
    auto& notif = std::get<JsonRpcNotification>(msg);
    EXPECT_EQ(notif.method, "notifications/initialized");
}

TEST(CodecParse, InvalidJson) {
    EXPECT_THROW(Codec::parse("{invalid json"), McpParseError);
}

TEST(CodecParse, MissingJsonrpc) {
    EXPECT_THROW(Codec::parse(R"({"id":1,"method":"ping"})"), McpParseError);
}

TEST(CodecParse, WrongJsonrpcVersion) {
    EXPECT_THROW(Codec::parse(R"({"jsonrpc":"1.0","id":1,"method":"ping"})"), McpParseError);
}

TEST(CodecParse, NullId) {
    EXPECT_THROW(Codec::parse(R"({"jsonrpc":"2.0","id":null,"method":"ping"})"), McpParseError);
}

TEST(CodecParse, EmptyInput) {
    EXPECT_THROW(Codec::parse(""), McpParseError);
}

TEST(CodecParse, NotAnObject) {
    EXPECT_THROW(Codec::parse("[1,2,3]"), McpParseError);
}

TEST(CodecParse, RequestWithParams) {
    auto msg = Codec::parse(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"echo","arguments":{"text":"hello"}}})");
    ASSERT_TRUE(std::holds_alternative<JsonRpcRequest>(msg));
    auto& req = std::get<JsonRpcRequest>(msg);
    ASSERT_TRUE(req.params.has_value());
    EXPECT_EQ(req.params->at("name"), "echo");
}

// ---- Batch parse tests ----

TEST(CodecParseBatch, ValidBatch) {
    auto msgs = Codec::parse_batch(R"([
        {"jsonrpc":"2.0","id":1,"method":"ping"},
        {"jsonrpc":"2.0","id":2,"method":"tools/list"},
        {"jsonrpc":"2.0","method":"notifications/initialized"}
    ])");
    ASSERT_EQ(msgs.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<JsonRpcRequest>(msgs[0]));
    EXPECT_TRUE(std::holds_alternative<JsonRpcRequest>(msgs[1]));
    EXPECT_TRUE(std::holds_alternative<JsonRpcNotification>(msgs[2]));
}

TEST(CodecParseBatch, EmptyBatch) {
    auto msgs = Codec::parse_batch("[]");
    EXPECT_TRUE(msgs.empty());
}

TEST(CodecParseBatch, NotAnArray) {
    EXPECT_THROW(Codec::parse_batch(R"({"jsonrpc":"2.0","id":1,"method":"ping"})"), McpParseError);
}

// ---- Serialize tests ----

TEST(CodecSerialize, Request) {
    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "ping";
    std::string out = Codec::serialize(req);
    auto parsed = Codec::parse(out);
    ASSERT_TRUE(std::holds_alternative<JsonRpcRequest>(parsed));
    EXPECT_EQ(std::get<JsonRpcRequest>(parsed).method, "ping");
}

TEST(CodecSerialize, Response) {
    JsonRpcResponse resp;
    resp.id = RequestId{int64_t{1}};
    resp.result = nlohmann::json{{"ok", true}};
    std::string out = Codec::serialize(resp);
    auto parsed = Codec::parse(out);
    ASSERT_TRUE(std::holds_alternative<JsonRpcResponse>(parsed));
}

TEST(CodecSerialize, Notification) {
    JsonRpcNotification notif;
    notif.method = "notifications/initialized";
    std::string out = Codec::serialize(notif);
    auto parsed = Codec::parse(out);
    ASSERT_TRUE(std::holds_alternative<JsonRpcNotification>(parsed));
}

TEST(CodecSerialize, RoundTrip) {
    const std::string original = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"cursor":"abc"}})";
    auto msg = Codec::parse(original);
    std::string serialized = Codec::serialize(msg);
    auto reparsed = Codec::parse(serialized);
    ASSERT_TRUE(std::holds_alternative<JsonRpcRequest>(reparsed));
    auto& req = std::get<JsonRpcRequest>(reparsed);
    EXPECT_EQ(req.method, "tools/list");
}

TEST(CodecSerialize, BatchRoundTrip) {
    std::vector<JsonRpcMessage> msgs;
    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "ping";
    msgs.push_back(req);

    JsonRpcNotification notif;
    notif.method = "notifications/initialized";
    msgs.push_back(notif);

    std::string out = Codec::serialize_batch(msgs);
    auto parsed = Codec::parse_batch(out);
    ASSERT_EQ(parsed.size(), 2u);
}

// ---- Large message test ----

TEST(CodecParse, LargeMessage) {
    // Build a tools/list response with 100 tools
    nlohmann::json tools = nlohmann::json::array();
    for (int i = 0; i < 100; ++i) {
        tools.push_back({
            {"name", "tool_" + std::to_string(i)},
            {"description", "Description for tool " + std::to_string(i)},
            {"inputSchema", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        });
    }
    nlohmann::json response = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"result", {{"tools", tools}}}
    };
    std::string raw = response.dump();
    auto msg = Codec::parse(raw);
    ASSERT_TRUE(std::holds_alternative<JsonRpcResponse>(msg));
}
