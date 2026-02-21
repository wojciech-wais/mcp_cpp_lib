#include <benchmark/benchmark.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include "mcp/codec.hpp"
#include <unistd.h>
#include <thread>
#include <chrono>

using namespace mcp;

// Set up a server+client pair over pipes for E2E benchmarking
struct E2EFixture {
    int c2s[2], s2c[2];  // client->server and server->client pipes
    std::unique_ptr<McpServer> server;
    std::unique_ptr<McpClient> client;
    std::thread server_thread;

    E2EFixture() {
        pipe(c2s);
        pipe(s2c);

        McpServer::Options sopts;
        sopts.server_info = {"bench-server", std::nullopt, "1.0"};
        sopts.thread_pool_size = 1;
        server = std::make_unique<McpServer>(sopts);

        // Register echo tool
        ToolDefinition echo_def;
        echo_def.name = "echo";
        echo_def.input_schema = nlohmann::json{{"type", "object"}};
        server->add_tool(echo_def, [](const nlohmann::json& args) -> CallToolResult {
            CallToolResult r;
            r.content.push_back(TextContent{args.value("text", ""), std::nullopt});
            return r;
        });

        auto server_transport = std::make_unique<StdioTransport>(c2s[0], s2c[1]);
        server_thread = std::thread([this, t = std::move(server_transport)]() mutable {
            server->serve(std::move(t));
        });

        McpClient::Options copts;
        copts.client_info = {"bench-client", std::nullopt, "1.0"};
        copts.request_timeout = std::chrono::milliseconds(5000);
        client = std::make_unique<McpClient>(copts);

        auto client_transport = std::make_unique<StdioTransport>(s2c[0], c2s[1]);
        client->connect(std::move(client_transport));

        // Initialize
        client->initialize();
    }

    ~E2EFixture() {
        client->disconnect();
        server->shutdown();
        if (server_thread.joinable()) server_thread.join();
        close(c2s[0]); close(c2s[1]);
        close(s2c[0]); close(s2c[1]);
    }
};

static void BM_ToolCallStdio(benchmark::State& state) {
    E2EFixture fixture;

    for (auto _ : state) {
        auto result = fixture.client->call_tool("echo", {{"text", "hello benchmark"}});
        benchmark::DoNotOptimize(result);
    }

    state.SetLabel("stdio tool/call roundtrip");
}
BENCHMARK(BM_ToolCallStdio)->MinTime(2.0)->UseRealTime();

static void BM_ListToolsStdio(benchmark::State& state) {
    E2EFixture fixture;
    // Add 100 tools
    for (int i = 0; i < 99; ++i) {
        ToolDefinition def;
        def.name = "tool_" + std::to_string(i);
        def.input_schema = nlohmann::json{{"type", "object"}};
        fixture.server->add_tool(def, [](const nlohmann::json&) -> CallToolResult { return {}; });
    }

    for (auto _ : state) {
        auto result = fixture.client->list_tools();
        benchmark::DoNotOptimize(result);
    }
    state.SetLabel("tools/list roundtrip");
}
BENCHMARK(BM_ListToolsStdio)->MinTime(2.0)->UseRealTime();

static void BM_PingStdio(benchmark::State& state) {
    E2EFixture fixture;

    for (auto _ : state) {
        fixture.client->ping();
    }
    state.SetLabel("ping roundtrip");
}
BENCHMARK(BM_PingStdio)->MinTime(2.0)->UseRealTime();
