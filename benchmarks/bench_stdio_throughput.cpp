#include <benchmark/benchmark.h>
#include "mcp/transport/stdio_transport.hpp"
#include "mcp/codec.hpp"
#include "mcp/server.hpp"
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>

using namespace mcp;

// Build a simple message for throughput testing
static std::string make_ping_request(int id) {
    return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"method":"ping"})";
}

static void BM_StdioThroughput(benchmark::State& state) {
    const int N = state.range(0);

    // Create pipe pairs
    int client_to_server[2], server_to_client[2];
    pipe(client_to_server);
    pipe(server_to_client);

    // Server transport: reads from client_to_server[0], writes to server_to_client[1]
    // Client transport: reads from server_to_client[0], writes to client_to_server[1]

    std::atomic<int> responses_received{0};

    // Start server
    McpServer::Options server_opts;
    server_opts.server_info = {"bench-server", std::nullopt, "1.0"};
    McpServer server{server_opts};

    auto server_transport = std::make_unique<StdioTransport>(
        client_to_server[0], server_to_client[1]);

    std::thread server_thread([&]() {
        server.serve(std::move(server_transport));
    });

    // Client transport
    StdioTransport client_transport(server_to_client[0], client_to_server[1]);

    std::thread client_recv([&]() {
        client_transport.start([&](JsonRpcMessage msg) {
            ++responses_received;
        });
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (auto _ : state) {
        // Send initialize + initialized
        std::string init_req = R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","clientInfo":{"name":"bench","version":"1.0"},"capabilities":{}}})";
        // Skip full handshake in benchmark - just measure ping throughput
        // In real benchmark we'd do initialization first

        responses_received = 0;
        for (int i = 1; i <= N; ++i) {
            std::string msg = make_ping_request(i) + "\n";
            write(client_to_server[1], msg.data(), msg.size());
        }

        // Wait for all responses
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (responses_received < N && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    server.shutdown();
    client_transport.shutdown();
    if (server_thread.joinable()) server_thread.join();
    if (client_recv.joinable()) client_recv.join();

    close(client_to_server[0]);
    close(client_to_server[1]);
    close(server_to_client[0]);
    close(server_to_client[1]);
}
// Note: This is a rough throughput benchmark; actual e2e benchmarks
// run in bench_e2e_tool_call.cpp
BENCHMARK(BM_StdioThroughput)->Arg(100)->MinTime(2.0);

static void BM_ParseAndSerialize1K(benchmark::State& state) {
    // Measure pure parse+serialize throughput (no transport)
    std::vector<std::string> messages;
    for (int i = 0; i < 1000; ++i) {
        messages.push_back(make_ping_request(i));
    }

    for (auto _ : state) {
        for (const auto& raw : messages) {
            auto msg = Codec::parse(raw);
            auto out = Codec::serialize(msg);
            benchmark::DoNotOptimize(out);
        }
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_ParseAndSerialize1K)->MinTime(1.0);
