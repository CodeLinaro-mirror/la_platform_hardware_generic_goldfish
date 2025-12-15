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

using network::Endpoint;
using network::ToEndpoint;
using network::ToIpAddress;

// Test fixture to manage common setup and teardown for socket benchmarks.
class SocketBenchmark : public ::benchmark::Fixture {
  public:
    void SetUp(const ::benchmark::State& state) override {
        factory_ = std::make_unique<LibuvAsyncSocketFactory>();
        loop_ = LibuvEventLoop::Create();
        loop_thread_ = std::thread([&]() { loop_->Run(); });
    }

    void TearDown(const ::benchmark::State& state) override {
        loop_->ShutdownAndWait();
        if (loop_thread_.joinable()) {
            loop_thread_.join();
        }
    }

    template <typename F>
    auto PostAndWait(F&& func) {
        return loop_->PostAndWait(std::forward<F>(func));
    }

  protected:
    std::unique_ptr<AsyncSocketFactory> factory_;
    std::unique_ptr<LibuvEventLoop> loop_;
    std::thread loop_thread_;
};

// Measures the round-trip latency (ping-pong) between client and server.
BENCHMARK_F(SocketBenchmark, PingPong)(benchmark::State& state) {
    std::string test_message = "ping";
    ScopedAsyncServer server(*PostAndWait([&](void) {
        auto endpoint = ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
        return factory_->CreateServer(loop_.get(), endpoint, [&](auto socket) {
            socket->SetOnReadCallbackNoFlowControl(
                    [s = socket](std::string_view data, auto status) {
                        if (status.ok()) s->Send(data.data(), data.size());
                    });
            // Let the test own the connection's lifetime.
            return true;
        });
    }));
    int port = *PostAndWait([&] { return server->Port(); });

    ScopedAsyncSocket client(*PostAndWait([&] {
        auto endpoint = ToEndpoint(ToIpAddress("127.0.0.1").value(), port);
        return factory_->CreateSocket(loop_.get(), endpoint);
    }));

    // Connect and wait for it to be established before starting the benchmark.
    absl::Notification connect_notification;
    loop_->Post([&]() {
        client->SetOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
            socket.SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {});
            connect_notification.Notify();
        });
        client->Connect();
    });
    connect_notification.WaitForNotification();

    for (const auto& _ : state) {
        state.PauseTiming();
        absl::Notification pong_notification;
        loop_->Post([&]() {
            client->SetOnReadCallbackNoFlowControl([&](auto, auto) { pong_notification.Notify(); });
        });
        state.ResumeTiming();

        loop_->Post([&]() { client->Send(test_message.c_str(), test_message.size()); });

        pong_notification.WaitForNotification();
    }
}

// Measures the maximum write throughput of a single socket.
BENCHMARK_F(SocketBenchmark, WriteThroughput)(benchmark::State& state) {
    const size_t buffer_size = 1024 * 1024;  // 1 MB buffer
    std::vector<char> buffer(buffer_size, 'A');
    ScopedAsyncSocket server_socket;

    ScopedAsyncServer server(*PostAndWait([&] {
        auto endpoint = ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
        return factory_->CreateServer(loop_.get(), endpoint, [&](auto socket) {
            server_socket = ScopedAsyncSocket(socket);
            socket->SetOnReadCallbackNoFlowControl([](auto, auto) {});  // Discard data.
            return true;
        });
    }));
    int port = *PostAndWait([&] { return server->Port(); });

    ScopedAsyncSocket client(*PostAndWait([&] {
        auto endpoint = ToEndpoint(ToIpAddress("127.0.0.1").value(), port);
        return factory_->CreateSocket(loop_.get(), endpoint);
    }));

    absl::Notification connected_notification;
    loop_->Post([&]() {
        client->SetOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
            socket.SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {});
            connected_notification.Notify();
        });
        client->Connect();
    });
    connected_notification.WaitForNotification();

    for (const auto& _ : state) {
        sendSynchronously(client.get(), buffer.data(), buffer.size());
    }

    state.SetBytesProcessed(state.iterations() * buffer_size);
}

}  // namespace
}  // namespace goldfish::async

BENCHMARK_MAIN();
