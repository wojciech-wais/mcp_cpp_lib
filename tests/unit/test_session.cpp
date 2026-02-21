#include <gtest/gtest.h>
#include "mcp/session.hpp"
#include <thread>
#include <chrono>

using namespace mcp;

TEST(Session, InitialState) {
    Session s;
    EXPECT_EQ(s.state(), SessionState::Uninitialized);
}

TEST(Session, StateTransitions) {
    Session s;
    s.set_state(SessionState::Initializing);
    EXPECT_EQ(s.state(), SessionState::Initializing);
    s.set_state(SessionState::Ready);
    EXPECT_EQ(s.state(), SessionState::Ready);
    s.set_state(SessionState::ShuttingDown);
    EXPECT_EQ(s.state(), SessionState::ShuttingDown);
    s.set_state(SessionState::Closed);
    EXPECT_EQ(s.state(), SessionState::Closed);
}

TEST(Session, NextIdIncreases) {
    Session s;
    auto id1 = s.next_id();
    auto id2 = s.next_id();
    auto id3 = s.next_id();
    ASSERT_TRUE(std::holds_alternative<int64_t>(id1));
    ASSERT_TRUE(std::holds_alternative<int64_t>(id2));
    ASSERT_TRUE(std::holds_alternative<int64_t>(id3));
    EXPECT_LT(std::get<int64_t>(id1), std::get<int64_t>(id2));
    EXPECT_LT(std::get<int64_t>(id2), std::get<int64_t>(id3));
}

TEST(Session, RegisterAndCompleteRequest) {
    Session s;
    bool callback_called = false;
    JsonRpcResponse received_resp;

    auto id = s.register_request("ping", [&](const JsonRpcResponse& resp) {
        callback_called = true;
        received_resp = resp;
    });

    EXPECT_TRUE(s.has_pending_request(id));

    JsonRpcResponse resp;
    resp.id = id;
    resp.result = nlohmann::json::object();

    bool completed = s.complete_request(id, resp);
    EXPECT_TRUE(completed);
    EXPECT_TRUE(callback_called);
    EXPECT_FALSE(s.has_pending_request(id));
}

TEST(Session, CompleteUnknownRequest) {
    Session s;
    RequestId id{int64_t{999}};
    JsonRpcResponse resp;
    resp.id = id;
    resp.result = nlohmann::json::object();

    bool completed = s.complete_request(id, resp);
    EXPECT_FALSE(completed);
}

TEST(Session, TimeoutDetection) {
    Session s;
    s.set_request_timeout(std::chrono::milliseconds(50));

    s.register_request("slow_method");

    // No timeouts yet
    auto timed_out = s.check_timeouts();
    EXPECT_TRUE(timed_out.empty());

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    timed_out = s.check_timeouts();
    EXPECT_EQ(timed_out.size(), 1u);
}

TEST(Session, CapabilitiesStorage) {
    Session s;
    s.server_capabilities().tools = nlohmann::json{{"listChanged", true}};
    s.client_capabilities().sampling = nlohmann::json::object();

    EXPECT_TRUE(s.server_capabilities().tools.has_value());
    EXPECT_TRUE(s.client_capabilities().sampling.has_value());
}

TEST(Session, ProtocolVersion) {
    Session s;
    s.protocol_version() = "2025-06-18";
    EXPECT_EQ(s.protocol_version(), "2025-06-18");
}

TEST(Session, SessionId) {
    Session s;
    EXPECT_FALSE(s.session_id().has_value());
    s.session_id() = "test-session-id";
    ASSERT_TRUE(s.session_id().has_value());
    EXPECT_EQ(*s.session_id(), "test-session-id");
}

TEST(Session, ThreadSafeNextId) {
    Session s;
    std::vector<int64_t> ids;
    std::mutex ids_mutex;

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < 100; ++j) {
                auto id = s.next_id();
                std::lock_guard<std::mutex> lock(ids_mutex);
                ids.push_back(std::get<int64_t>(id));
            }
        });
    }
    for (auto& t : threads) t.join();

    // All IDs should be unique
    std::sort(ids.begin(), ids.end());
    auto it = std::unique(ids.begin(), ids.end());
    EXPECT_EQ(it, ids.end());
}
