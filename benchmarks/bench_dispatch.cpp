#include <benchmark/benchmark.h>
#include "mcp/router.hpp"
#include "mcp/types.hpp"
#include <memory>
#include <string>
#include <vector>

using namespace mcp;

// Create a router with N methods registered (returns via unique_ptr to avoid mutex copy)
static std::unique_ptr<Router> make_router(int n_methods) {
    auto router = std::make_unique<Router>();
    for (int i = 0; i < n_methods; ++i) {
        router->on_request("method_" + std::to_string(i),
            [](const nlohmann::json&) -> HandlerResult {
                return nlohmann::json{{"result", "ok"}};
            });
    }
    router->on_request("ping", [](const nlohmann::json&) -> HandlerResult {
        return nlohmann::json::object();
    });
    router->on_request("tools/list", [](const nlohmann::json&) -> HandlerResult {
        return nlohmann::json{{"tools", nlohmann::json::array()}};
    });
    return router;
}

static void BM_DispatchKnownMethod(benchmark::State& state) {
    auto router = make_router(1);

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "ping";

    for (auto _ : state) {
        auto resp = router->dispatch(req);
        benchmark::DoNotOptimize(resp);
    }
}
BENCHMARK(BM_DispatchKnownMethod)->MinTime(1.0);

static void BM_DispatchUnknownMethod(benchmark::State& state) {
    auto router = make_router(1);

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "not_registered_method";

    for (auto _ : state) {
        auto resp = router->dispatch(req);
        benchmark::DoNotOptimize(resp);
    }
}
BENCHMARK(BM_DispatchUnknownMethod)->MinTime(1.0);

static void BM_DispatchWithCapCheck(benchmark::State& state) {
    auto router = make_router(1);
    router->on_request("tools/call", [](const nlohmann::json&) -> HandlerResult {
        return nlohmann::json{{"content", nlohmann::json::array()}};
    });
    router->require_capability("tools/call", "tools");

    ServerCapabilities caps;
    caps.tools = nlohmann::json{{"listChanged", true}};
    router->set_capabilities(caps, ClientCapabilities{});

    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "tools/call";
    req.params = nlohmann::json{{"name", "echo"}, {"arguments", nlohmann::json::object()}};

    for (auto _ : state) {
        auto resp = router->dispatch(req);
        benchmark::DoNotOptimize(resp);
    }
}
BENCHMARK(BM_DispatchWithCapCheck)->MinTime(1.0);

static void BM_Dispatch100Methods(benchmark::State& state) {
    auto router = make_router(100);

    std::vector<JsonRpcRequest> requests;
    for (int i = 0; i < 100; ++i) {
        JsonRpcRequest req;
        req.id = RequestId{int64_t{i}};
        req.method = "method_" + std::to_string(i);
        requests.push_back(req);
    }

    int i = 0;
    for (auto _ : state) {
        auto resp = router->dispatch(requests[i % 100]);
        benchmark::DoNotOptimize(resp);
        ++i;
    }
}
BENCHMARK(BM_Dispatch100Methods)->MinTime(1.0);

static void BM_DispatchNotification(benchmark::State& state) {
    Router router;
    router.on_notification("notifications/initialized", [](const nlohmann::json&) {});

    JsonRpcNotification notif;
    notif.method = "notifications/initialized";

    for (auto _ : state) {
        auto resp = router.dispatch(notif);
        benchmark::DoNotOptimize(resp);
    }
}
BENCHMARK(BM_DispatchNotification)->MinTime(1.0);
