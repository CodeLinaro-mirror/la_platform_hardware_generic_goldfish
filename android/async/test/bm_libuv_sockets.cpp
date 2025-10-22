#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "benchmark/benchmark.h"

#include "goldfish/async/async_socket_utils.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"

namespace goldfish::async {
namespace {

// Test fixture to manage common setup and teardown for socket benchmarks.
class SocketBenchmark : public ::benchmark::Fixture {
  public:
    void SetUp(const ::benchmark::State& state) override {
        factory_ = std::make_unique<LibuvAsyncSocketFactory>();
        loop_ = LibuvEventLoop::create();
        loop_thread_ = std::thread([&]() { loop_->run(); });
    }

    void TearDown(const ::benchmark::State& state) override {
        loop_->stop();
        if (loop_thread_.joinable()) {
            loop_thread_.join();
        }
    }

    template <typename F>
    auto postAndWait(F&& func) {
        return loop_->postAndWait(std::forward<F>(func));
    }

  protected:
    std::unique_ptr<AsyncSocketFactory> factory_;
    std::unique_ptr<EventLoop> loop_;
    std::thread loop_thread_;
};

// Measures the round-trip latency (ping-pong) between client and server.
BENCHMARK_F(SocketBenchmark, PingPong)(benchmark::State& state) {
    std::string test_message = "ping";
    ScopedAsyncServer server(postAndWait([&](void) {
        return factory_->createServer(loop_.get(), "127.0.0.1:0", [&](auto socket) {
            socket->setOnReadCallback([s = socket](std::string_view data, auto status) {
                if (status.ok()) s->send(data.data(), data.size());
            });
            // Let the test own the connection's lifetime.
            return true;
        });
    }));
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&] {
        return factory_->createSocket(loop_.get(), "127.0.0.1:" + std::to_string(port));
    }));

    // Connect and wait for it to be established before starting the benchmark.
    absl::Notification connect_notification;
    loop_->post([&]() {
        client->setOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
            socket.setOnReadCallback([&](std::string_view data, absl::Status err) {});
            connect_notification.Notify();
        });
        client->connect();
    });
    connect_notification.WaitForNotification();

    for (auto _ : state) {
        state.PauseTiming();
        absl::Notification pong_notification;
        loop_->post([&]() {
            client->setOnReadCallback([&](auto, auto) { pong_notification.Notify(); });
        });
        state.ResumeTiming();

        loop_->post([&]() { client->send(test_message.c_str(), test_message.size()); });

        pong_notification.WaitForNotification();
    }
}

// Measures the maximum write throughput of a single socket.
BENCHMARK_F(SocketBenchmark, WriteThroughput)(benchmark::State& state) {
    const size_t buffer_size = 1024 * 1024;  // 1 MB buffer
    std::vector<char> buffer(buffer_size, 'A');
    ScopedAsyncSocket server_socket;

    ScopedAsyncServer server(postAndWait([&] {
        return factory_->createServer(loop_.get(), "127.0.0.1:0", [&](auto socket) {
            server_socket = ScopedAsyncSocket(socket);
            socket->setOnReadCallback([](auto, auto) {});  // Discard data.
            return true;
        });
    }));
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&] {
        return factory_->createSocket(loop_.get(), "127.0.0.1:" + std::to_string(port));
    }));

    absl::Notification connected_notification;
    loop_->post([&]() {
        client->setOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
            socket.setOnReadCallback([&](std::string_view data, absl::Status err) {});
            connected_notification.Notify();
        });
        client->connect();
    });
    connected_notification.WaitForNotification();

    for (auto _ : state) {
        sendSynchronously(client.get(), buffer.data(), buffer.size());
    }

    state.SetBytesProcessed(state.iterations() * buffer_size);
}

}  // namespace
}  // namespace goldfish::async

BENCHMARK_MAIN();
