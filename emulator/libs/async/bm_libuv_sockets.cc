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
        loop_thread_ = std::thread([&]() { loop_->Run().IgnoreError(); });
    }

    void TearDown(const ::benchmark::State& state) override {
        loop_->ShutdownAndWait().IgnoreError();
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
                        if (status.ok()) s->Send(data.data(), data.size()).IgnoreError();
                    });
            // Let the test own the connection's lifetime.
            return true;
        });
    }));
    Endpoint endpoint = *PostAndWait([&] { return server->GetEndpoint(); });

    ScopedAsyncSocket client(
            *PostAndWait([&, endpoint] { return factory_->CreateSocket(loop_.get(), endpoint); }));

    // Connect and wait for it to be established before starting the benchmark.
    absl::Notification connect_notification;
    loop_->Post([&]() {
             client->SetOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
                 socket.SetOnReadCallbackNoFlowControl(
                         [&](std::string_view data, absl::Status err) {});
                 connect_notification.Notify();
             });
             client->Connect().IgnoreError();
         }).IgnoreError();
    connect_notification.WaitForNotification();

    for (const auto& _ : state) {
        state.PauseTiming();
        absl::Notification pong_notification;
        loop_->Post([&]() {
                 client->SetOnReadCallbackNoFlowControl(
                         [&](auto, auto) { pong_notification.Notify(); });
             }).IgnoreError();
        state.ResumeTiming();

        loop_->Post([&]() {
                 client->Send(test_message.c_str(), test_message.size()).IgnoreError();
             }).IgnoreError();

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
    Endpoint endpoint = *PostAndWait([&] { return server->GetEndpoint(); });

    ScopedAsyncSocket client(
            *PostAndWait([&, endpoint] { return factory_->CreateSocket(loop_.get(), endpoint); }));

    absl::Notification connected_notification;
    loop_->Post([&]() {
             client->SetOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
                 socket.SetOnReadCallbackNoFlowControl(
                         [&](std::string_view data, absl::Status err) {});
                 connected_notification.Notify();
             });
             client->Connect().IgnoreError();
         }).IgnoreError();
    connected_notification.WaitForNotification();

    for (const auto& _ : state) {
        sendSynchronously(client.get(), buffer.data(), buffer.size()).IgnoreError();
    }

    state.SetBytesProcessed(state.iterations() * buffer_size);
}

// Measures write/read throughput when calling Send(data, size) directly on the
// loop thread (matching HostToGuestConnection::OnReceive in vsock_port_fwd).
BENCHMARK_DEFINE_F(SocketBenchmark, LoopThreadSendThroughput)(benchmark::State& state) {
    const size_t chunk_size = static_cast<size_t>(state.range(0));
    constexpr size_t kBatchBytes = 4 * 1024 * 1024;  // 4 MiB per iteration batch
    const size_t chunks_per_batch = kBatchBytes / chunk_size;
    std::string chunk(chunk_size, 'B');

    ScopedAsyncSocket server_socket;
    size_t bytes_received = 0;
    size_t target_bytes = 0;
    uint64_t read_callbacks = 0;
    absl::Notification* batch_done = nullptr;

    ScopedAsyncServer server(*PostAndWait([&] {
        auto endpoint = ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
        return factory_->CreateServer(loop_.get(), endpoint, [&](auto socket) {
            server_socket = ScopedAsyncSocket(socket);
            socket->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status status) {
                if (!status.ok()) return;
                ++read_callbacks;
                bytes_received += data.size();
                if (batch_done != nullptr && bytes_received >= target_bytes) {
                    absl::Notification* done = batch_done;
                    batch_done = nullptr;
                    done->Notify();
                }
            });
            return true;
        });
    }));
    Endpoint endpoint = *PostAndWait([&] { return server->GetEndpoint(); });

    ScopedAsyncSocket client(
            *PostAndWait([&, endpoint] { return factory_->CreateSocket(loop_.get(), endpoint); }));

    absl::Notification connected_notification;
    loop_->Post([&]() {
             client->SetOnConnectedCallback([&](AsyncSocket& socket, absl::Status) {
                 socket.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
                 connected_notification.Notify();
             });
             client->Connect().IgnoreError();
         }).IgnoreError();
    connected_notification.WaitForNotification();

    const uint64_t start_callbacks = read_callbacks;
    for (const auto& _ : state) {
        absl::Notification done;
        loop_->Post([&]() {
                 bytes_received = 0;
                 target_bytes = chunks_per_batch * chunk_size;
                 batch_done = &done;
                 for (size_t i = 0; i < chunks_per_batch; ++i) {
                     client->Send(chunk.data(), chunk.size()).IgnoreError();
                 }
             }).IgnoreError();
        done.WaitForNotification();
    }

    const uint64_t total_callbacks = read_callbacks - start_callbacks;
    state.counters["callbacks/op"] = benchmark::Counter(static_cast<double>(total_callbacks) /
                                                        static_cast<double>(state.iterations()));
    state.SetBytesProcessed(state.iterations() * chunks_per_batch * chunk_size);
}

BENCHMARK_REGISTER_F(SocketBenchmark, LoopThreadSendThroughput)
        ->Arg(4096)
        ->Arg(16384)
        ->Arg(65536)
        ->Arg(262144)
        ->Arg(1048576)
        ->UseRealTime();

// Measures LibuvSocket read-slab allocation + cross-loop handoff throughput,
// matching the ADB host-to-guest path where an external peer writes a payload
// into the TCP socket and LibuvSocket's OnReadCallback copies each read slab
// into a std::string and posts it to a consumer EventLoop.
BENCHMARK_DEFINE_F(SocketBenchmark, ReadSlabCrossLoopHandoff)(benchmark::State& state) {
    const size_t payload_size = static_cast<size_t>(state.range(0));
    std::string payload(payload_size, 'R');

    auto consumer_loop = LibuvEventLoop::Create();
    std::thread consumer_thread([&]() { consumer_loop->Run().IgnoreError(); });

    ScopedAsyncSocket server_socket;
    size_t consumer_bytes_received = 0;
    size_t target_bytes = 0;
    uint64_t read_callbacks = 0;
    absl::Notification* transfer_done = nullptr;

    ScopedAsyncServer server(*PostAndWait([&] {
        auto endpoint = ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
        return factory_->CreateServer(loop_.get(), endpoint, [&](auto socket) {
            server_socket = ScopedAsyncSocket(socket);
            socket->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status status) {
                if (!status.ok() || data.empty()) return;
                ++read_callbacks;
                std::string copied(data);
                consumer_loop
                        ->Post([&, chunk = std::move(copied)]() {
                            benchmark::DoNotOptimize(chunk.data());
                            consumer_bytes_received += chunk.size();
                            if (transfer_done != nullptr &&
                                consumer_bytes_received >= target_bytes) {
                                absl::Notification* done = transfer_done;
                                transfer_done = nullptr;
                                done->Notify();
                            }
                        })
                        .IgnoreError();
            });
            return true;
        });
    }));
    Endpoint endpoint = *PostAndWait([&] { return server->GetEndpoint(); });

    auto sender_loop = LibuvEventLoop::Create();
    std::thread sender_thread([&]() { sender_loop->Run().IgnoreError(); });
    ScopedAsyncSocket client(*sender_loop->PostAndWait(
            [&, endpoint] { return factory_->CreateSocket(sender_loop.get(), endpoint); }));

    absl::Notification connected_notification;
    sender_loop
            ->Post([&]() {
                client->SetOnConnectedCallback([&](AsyncSocket& socket, absl::Status) {
                    socket.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
                    connected_notification.Notify();
                });
                client->Connect().IgnoreError();
            })
            .IgnoreError();
    connected_notification.WaitForNotification();

    const uint64_t start_callbacks = read_callbacks;
    for (const auto& _ : state) {
        absl::Notification done;
        consumer_loop
                ->PostAndWait([&]() {
                    consumer_bytes_received = 0;
                    target_bytes = payload_size;
                    transfer_done = &done;
                })
                .IgnoreError();
        sender_loop->Post([&]() { client->Send(payload.data(), payload.size()).IgnoreError(); })
                .IgnoreError();
        done.WaitForNotification();
    }

    const uint64_t total_callbacks = read_callbacks - start_callbacks;
    state.counters["callbacks/op"] = benchmark::Counter(static_cast<double>(total_callbacks) /
                                                        static_cast<double>(state.iterations()));
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));

    client = ScopedAsyncSocket();
    sender_loop->ShutdownAndWait().IgnoreError();
    if (sender_thread.joinable()) {
        sender_thread.join();
    }
    consumer_loop->ShutdownAndWait().IgnoreError();
    if (consumer_thread.joinable()) {
        consumer_thread.join();
    }
}

BENCHMARK_REGISTER_F(SocketBenchmark, ReadSlabCrossLoopHandoff)
        ->Arg(65536)
        ->Arg(262144)
        ->Arg(1048576)
        ->UseRealTime();

}  // namespace
}  // namespace goldfish::async

BENCHMARK_MAIN();
