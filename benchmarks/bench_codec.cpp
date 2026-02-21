#include <benchmark/benchmark.h>
#include "mcp/codec.hpp"
#include "mcp/json_rpc.hpp"
#include <string>
#include <vector>

using namespace mcp;

// Small message (~100 bytes)
static const std::string kSmallRequest =
    R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{}})";

// Tool call request
static const std::string kToolCallRequest =
    R"({"jsonrpc":"2.0","id":42,"method":"tools/call","params":{"name":"get_weather","arguments":{"location":"Warsaw","units":"celsius"}}})";

// Generate a large tools/list response with N tools
static std::string make_large_response(int n) {
    nlohmann::json tools = nlohmann::json::array();
    for (int i = 0; i < n; ++i) {
        tools.push_back({
            {"name", "tool_" + std::to_string(i)},
            {"description", "A tool for doing something useful, number " + std::to_string(i)},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"param1", {{"type", "string"}, {"description", "First parameter"}}},
                    {"param2", {{"type", "integer"}, {"description", "Second parameter"}}}
                }},
                {"required", {"param1"}}
            }}
        });
    }
    nlohmann::json resp = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"result", {{"tools", tools}}}
    };
    return resp.dump();
}

static const std::string kLargeResponse = make_large_response(100);

// ---- Parse benchmarks ----

static void BM_ParseSmallMessage(benchmark::State& state) {
    for (auto _ : state) {
        auto msg = Codec::parse(kSmallRequest);
        benchmark::DoNotOptimize(msg);
    }
    state.SetBytesProcessed(state.iterations() * kSmallRequest.size());
}
BENCHMARK(BM_ParseSmallMessage)->MinTime(1.0);

static void BM_ParseToolCallRequest(benchmark::State& state) {
    for (auto _ : state) {
        auto msg = Codec::parse(kToolCallRequest);
        benchmark::DoNotOptimize(msg);
    }
    state.SetBytesProcessed(state.iterations() * kToolCallRequest.size());
}
BENCHMARK(BM_ParseToolCallRequest)->MinTime(1.0);

static void BM_ParseLargeMessage(benchmark::State& state) {
    for (auto _ : state) {
        auto msg = Codec::parse(kLargeResponse);
        benchmark::DoNotOptimize(msg);
    }
    state.SetBytesProcessed(state.iterations() * kLargeResponse.size());
}
BENCHMARK(BM_ParseLargeMessage)->MinTime(1.0);

static void BM_ParseBatch(benchmark::State& state) {
    // Build a batch of 50 ping requests
    nlohmann::json batch = nlohmann::json::array();
    for (int i = 0; i < 50; ++i) {
        batch.push_back({{"jsonrpc", "2.0"}, {"id", i}, {"method", "ping"}, {"params", nlohmann::json::object()}});
    }
    std::string raw = batch.dump();

    for (auto _ : state) {
        auto msgs = Codec::parse_batch(raw);
        benchmark::DoNotOptimize(msgs);
    }
    state.SetBytesProcessed(state.iterations() * raw.size());
}
BENCHMARK(BM_ParseBatch)->MinTime(1.0);

static void BM_ParseInvalidJson(benchmark::State& state) {
    const std::string bad = "{this is not valid json at all!!!";
    for (auto _ : state) {
        try {
            auto msg = Codec::parse(bad);
            benchmark::DoNotOptimize(msg);
        } catch (...) {}
    }
}
BENCHMARK(BM_ParseInvalidJson)->MinTime(1.0);

// ---- Serialize benchmarks ----

static void BM_SerializeSmallMessage(benchmark::State& state) {
    JsonRpcRequest req;
    req.id = RequestId{int64_t{1}};
    req.method = "ping";
    req.params = nlohmann::json::object();

    for (auto _ : state) {
        auto s = Codec::serialize(req);
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_SerializeSmallMessage)->MinTime(1.0);

static void BM_SerializeLargeMessage(benchmark::State& state) {
    // Pre-parse the large response
    auto msg = Codec::parse(kLargeResponse);

    for (auto _ : state) {
        auto s = Codec::serialize(msg);
        benchmark::DoNotOptimize(s);
    }
    state.SetBytesProcessed(state.iterations() * kLargeResponse.size());
}
BENCHMARK(BM_SerializeLargeMessage)->MinTime(1.0);

static void BM_RoundTrip(benchmark::State& state) {
    for (auto _ : state) {
        auto msg = Codec::parse(kToolCallRequest);
        auto serialized = Codec::serialize(msg);
        benchmark::DoNotOptimize(serialized);
    }
}
BENCHMARK(BM_RoundTrip)->MinTime(1.0);
