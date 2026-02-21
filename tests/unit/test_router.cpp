#include <gtest/gtest.h>
#include "mcp/router.hpp"
#include "mcp/error.hpp"
#include <string>

using namespace mcp;

TEST(Router, DispatchKnownRequest) {
    Router router;
    bool called = false;
    router.on_request("ping", [&called](const nlohmann::json&) -> HandlerResult {
        called = true;
        return nlohmann::json::object();
    });

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "ping";

    auto response = router.dispatch(req);
    ASSERT_TRUE(response.has_value());
    EXPECT_TRUE(std::holds_alternative<JsonRpcResponse>(*response));
    EXPECT_TRUE(called);
}

TEST(Router, DispatchUnknownMethod) {
    Router router;

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "unknown/method";

    auto response = router.dispatch(req);
    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(std::holds_alternative<JsonRpcResponse>(*response));
    auto& resp = std::get<JsonRpcResponse>(*response);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, error::MethodNotFound);
}

TEST(Router, DispatchNotification) {
    Router router;
    bool called = false;
    router.on_notification("notifications/initialized", [&called](const nlohmann::json&) {
        called = true;
    });

    JsonRpcNotification notif;
    notif.method = "notifications/initialized";

    auto response = router.dispatch(notif);
    EXPECT_FALSE(response.has_value());
    EXPECT_TRUE(called);
}

TEST(Router, DispatchUnknownNotification) {
    Router router;
    // Unknown notification should be silently ignored
    JsonRpcNotification notif;
    notif.method = "unknown/notification";
    auto response = router.dispatch(notif);
    EXPECT_FALSE(response.has_value());
}

TEST(Router, HandlerThrowsMcpProtocolError) {
    Router router;
    router.on_request("fail", [](const nlohmann::json&) -> HandlerResult {
        throw McpProtocolError(error::InvalidParams, "Bad params");
        return nlohmann::json{};
    });

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "fail";

    auto response = router.dispatch(req);
    ASSERT_TRUE(response.has_value());
    auto& resp = std::get<JsonRpcResponse>(*response);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, error::InvalidParams);
}

TEST(Router, HandlerThrowsStdException) {
    Router router;
    router.on_request("fail", [](const nlohmann::json&) -> HandlerResult {
        throw std::runtime_error("internal failure");
        return nlohmann::json{};
    });

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "fail";

    auto response = router.dispatch(req);
    ASSERT_TRUE(response.has_value());
    auto& resp = std::get<JsonRpcResponse>(*response);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, error::InternalError);
}

TEST(Router, CapabilityGating_Blocked) {
    Router router;
    router.on_request("tools/list", [](const nlohmann::json&) -> HandlerResult {
        return nlohmann::json{{"tools", nlohmann::json::array()}};
    });
    // Require "tools" capability but don't set it
    router.require_capability("tools/list", "tools");

    // No capabilities set - should be blocked
    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "tools/list";

    auto response = router.dispatch(req);
    ASSERT_TRUE(response.has_value());
    auto& resp = std::get<JsonRpcResponse>(*response);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, error::InvalidRequest);
}

TEST(Router, CapabilityGating_Allowed) {
    Router router;
    router.on_request("tools/list", [](const nlohmann::json&) -> HandlerResult {
        return nlohmann::json{{"tools", nlohmann::json::array()}};
    });
    router.require_capability("tools/list", "tools");

    // Set tools capability
    ServerCapabilities caps;
    caps.tools = nlohmann::json{{"listChanged", true}};
    router.set_capabilities(caps, ClientCapabilities{});

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "tools/list";

    auto response = router.dispatch(req);
    ASSERT_TRUE(response.has_value());
    auto& resp = std::get<JsonRpcResponse>(*response);
    EXPECT_FALSE(resp.error.has_value());
}

TEST(Router, HandlerReturnsError) {
    Router router;
    router.on_request("fail", [](const nlohmann::json&) -> HandlerResult {
        return JsonRpcError{error::InvalidParams, "Missing required field", std::nullopt};
    });

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "fail";

    auto response = router.dispatch(req);
    ASSERT_TRUE(response.has_value());
    auto& resp = std::get<JsonRpcResponse>(*response);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, error::InvalidParams);
}

TEST(Router, HasHandler) {
    Router router;
    router.on_request("ping", [](const nlohmann::json&) -> HandlerResult {
        return nlohmann::json::object();
    });
    EXPECT_TRUE(router.has_handler("ping"));
    EXPECT_FALSE(router.has_handler("unknown"));
}

TEST(Router, DispatchResponse) {
    Router router;
    // Responses should not be dispatched
    JsonRpcResponse resp;
    resp.id = RequestId{int64_t{1}};
    resp.result = nlohmann::json::object();

    auto result = router.dispatch(resp);
    EXPECT_FALSE(result.has_value());
}
