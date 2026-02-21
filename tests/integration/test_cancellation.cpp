#include <gtest/gtest.h>
#include "mcp/server.hpp"
#include "mcp/client.hpp"
#include "mcp/transport/stdio_transport.hpp"
#include <unistd.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace mcp;

TEST(CancellationTest, CancelSentNotification) {
    int c2s[2], s2c[2];
    ASSERT_EQ(pipe(c2s), 0);
    ASSERT_EQ(pipe(s2c), 0);

    McpServer::Options sopts;
    sopts.server_info = {"cancel-server", std::nullopt, "1.0"};
    McpServer server{sopts};

    auto server_transport = std::make_unique<StdioTransport>(c2s[0], s2c[1]);
    std::thread server_thread([&, t = std::move(server_transport)]() mutable {
        server.serve(std::move(t));
    });

    McpClient::Options copts;
    copts.client_info = {"test-client", std::nullopt, "1.0"};
    copts.request_timeout = std::chrono::milliseconds(5000);
    McpClient client{copts};
    auto client_transport = std::make_unique<StdioTransport>(s2c[0], c2s[1]);
    client.connect(std::move(client_transport));
    client.initialize();

    // Send a cancel notification for a non-existent request
    // This should not cause any errors
    EXPECT_NO_THROW(client.cancel_request(RequestId{int64_t{999}}, "test cancel"));

    client.disconnect();
    server.shutdown();
    if (server_thread.joinable()) server_thread.join();
    close(c2s[0]); close(c2s[1]);
    close(s2c[0]); close(s2c[1]);
}
