#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"

#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::async {
namespace {

using namespace std::chrono_literals;
using absl_testing::IsOk;
using network::ToEndpoint;
using network::ToIpAddress;

class LoopHandoffTest : public ::testing::Test {
  protected:
    void SetUp() override {
        server_loop_ = LibuvEventLoop::Create();
        factory_ = std::make_unique<LibuvAsyncSocketFactory>();
        server_thread_ = std::thread([this] { server_loop_->Run().IgnoreError(); });

        // Wait for loop to start.
        while (server_loop_->GetState() != LooperStatusEvent::State::kRunning) {
            std::this_thread::sleep_for(10ms);
        }
    }

    void TearDown() override {
        server_loop_->ShutdownAndWait(100ms).IgnoreError();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    std::unique_ptr<LibuvEventLoop> server_loop_;
    std::shared_ptr<AsyncSocketFactory> factory_;
    std::thread server_thread_;
};

TEST_F(LoopHandoffTest, HandoffToDedicatedThread) {
    absl::Notification connected_on_target;
    absl::Notification client_closed;
    std::atomic<std::thread::id> target_thread_id;
    std::shared_ptr<AsyncSocket> server_side_client;
    std::vector<std::unique_ptr<ThreadedEventLoop>> target_loops;

    // 1. Define the LoopProvider: Create a new thread for each connection.
    AsyncSocketServer::LoopProvider loop_provider = [&](const network::Endpoint&) {
        auto target_loop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
        auto* ptr = target_loop.get();
        target_loops.push_back(std::move(target_loop));
        return ptr;
    };

    // 2. Start the server with the LoopProvider.
    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        VLOG(1) << "HandoffToDedicatedThread: on_connect triggered for socket=" << socket.get();
        target_thread_id = std::this_thread::get_id();
        socket->SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
        socket->SetOnCloseCallback([&] {
            VLOG(1) << "Client socket closed on target loop";
            client_closed.Notify();
        });
        server_side_client = std::move(socket);
        connected_on_target.Notify();
        return true;
    };

    ScopedAsyncServer server(server_loop_
                                     ->PostAndWait([&] {
                                         auto endpoint =
                                                 ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
                                         return factory_->CreateServer(server_loop_.get(), endpoint,
                                                                       on_connect, loop_provider);
                                     })
                                     .value());

    ASSERT_NE(server, nullptr);
    auto server_endpoint = server_loop_->PostAndWait([&] { return server->GetEndpoint(); }).value();

    // 3. Connect from a client.
    ScopedAsyncSocket client(server_loop_
                                     ->PostAndWait([&] {
                                         return factory_->CreateSocket(server_loop_.get(),
                                                                       server_endpoint);
                                     })
                                     .value());

    server_loop_
            ->Post([&] {
                client->SetOnConnectedCallback([](AsyncSocket& s, absl::Status err) {
                    EXPECT_THAT(err, IsOk());
                    s.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
                });
                client->Connect().IgnoreError();
            })
            .IgnoreError();

    // 4. Verify that the server's OnConnect ran on a different thread.
    connected_on_target.WaitForNotification();
    EXPECT_NE(target_thread_id.load(), std::this_thread::get_id());
    EXPECT_NE(target_thread_id.load(), server_thread_.get_id());

    // Clean up.
    if (server_side_client) {
        auto loop = server_side_client->GetLoop();
        loop->Post([s = server_side_client] { s->Close(); }).IgnoreError();
    }

    // Make sure we don't have a dangling client on our event loop.
    client_closed.WaitForNotification();
}

TEST_F(LoopHandoffTest, FallbackToServerLoop) {
    absl::Notification connected_on_target;
    absl::Notification client_closed;
    std::atomic<std::thread::id> target_thread_id;
    std::shared_ptr<AsyncSocket> server_side_client;

    // LoopProvider explicitly returns nullptr.
    AsyncSocketServer::LoopProvider loop_provider = [&](const network::Endpoint&) {
        return nullptr;
    };

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        VLOG(1) << "FallbackToServerLoop: on_connect triggered for socket=" << socket.get();
        target_thread_id = std::this_thread::get_id();
        socket->SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
        socket->SetOnCloseCallback([&] { client_closed.Notify(); });
        server_side_client = std::move(socket);
        connected_on_target.Notify();
        return true;
    };

    ScopedAsyncServer server(server_loop_
                                     ->PostAndWait([&] {
                                         auto endpoint =
                                                 ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
                                         return factory_->CreateServer(server_loop_.get(), endpoint,
                                                                       on_connect, loop_provider);
                                     })
                                     .value());

    auto server_endpoint = server_loop_->PostAndWait([&] { return server->GetEndpoint(); }).value();

    ScopedAsyncSocket client(server_loop_
                                     ->PostAndWait([&] {
                                         return factory_->CreateSocket(server_loop_.get(),
                                                                       server_endpoint);
                                     })
                                     .value());

    server_loop_
            ->Post([&] {
                client->SetOnConnectedCallback([](AsyncSocket& s, absl::Status err) {
                    EXPECT_THAT(err, IsOk());
                    s.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
                });
                client->Connect().IgnoreError();
            })
            .IgnoreError();

    connected_on_target.WaitForNotification();
    // Verify it ran on the server's loop thread.
    EXPECT_EQ(target_thread_id.load(), server_thread_.get_id());

    if (server_side_client) {
        auto loop = server_side_client->GetLoop();
        loop->Post([s = server_side_client] { s->Close(); }).IgnoreError();
    }
    client_closed.WaitForNotification();
}

TEST_F(LoopHandoffTest, NoDataLossDuringHandoff) {
    const std::string preamble = "PREAMBLE_DATA";
    const std::string mid_handoff_data = "MID_HANDOFF_DATA";
    const std::string expected_data = preamble + mid_handoff_data;

    absl::Notification handoff_started;
    absl::Notification data_received_on_target;
    std::string total_received_data;

    auto target_loop = ThreadedEventLoop::Create(LibuvEventLoop::Create());

    // 1. Define LoopProvider that signals handoff is about to happen.
    AsyncSocketServer::LoopProvider loop_provider = [&](const network::Endpoint&) {
        VLOG(1) << "LoopProvider: signaling handoff start";
        handoff_started.Notify();
        return target_loop.get();
    };

    // 2. Server's ConnectCallback.
    std::shared_ptr<AsyncSocket> server_side_client;
    absl::Notification client_closed;
    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        VLOG(1) << "Server ConnectCallback triggered on target loop";
        socket->SetOnCloseCallback([&] { client_closed.Notify(); });
        socket->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status status) {
            VLOG(1) << "Server received data: " << data.size() << " bytes";
            EXPECT_THAT(status, IsOk());
            total_received_data += std::string(data);
            if (total_received_data.size() >= expected_data.size()) {
                data_received_on_target.Notify();
            }
        });
        server_side_client = std::move(socket);
        return true;
    };

    // Start server.
    ScopedAsyncServer server(server_loop_
                                     ->PostAndWait([&] {
                                         auto endpoint =
                                                 ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
                                         return factory_->CreateServer(server_loop_.get(), endpoint,
                                                                       on_connect, loop_provider);
                                     })
                                     .value());

    auto server_endpoint = server_loop_->PostAndWait([&] { return server->GetEndpoint(); }).value();

    // 3. Client connects.
    ScopedAsyncSocket client(server_loop_
                                     ->PostAndWait([&] {
                                         return factory_->CreateSocket(server_loop_.get(),
                                                                       server_endpoint);
                                     })
                                     .value());

    absl::Notification client_connected;
    server_loop_
            ->Post([&] {
                client->SetOnConnectedCallback([&](AsyncSocket& s, absl::Status err) {
                    EXPECT_THAT(err, IsOk());
                    s.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
                    client_connected.Notify();
                });
                client->Connect().IgnoreError();
            })
            .IgnoreError();

    ASSERT_TRUE(client_connected.WaitForNotificationWithTimeout(absl::Seconds(5)));

    // Send preamble data immediately. This data arrives while the server is
    // potentially in the process of handoff.
    server_loop_
            ->Post([&] {
                VLOG(1) << "Client sending preamble";
                client->Send(preamble.data(), preamble.size()).IgnoreError();
            })
            .IgnoreError();

    // 4. Wait for server to enter LoopProvider.
    ASSERT_TRUE(handoff_started.WaitForNotificationWithTimeout(absl::Seconds(5)));

    // 5. Send more data while handoff is in progress.
    server_loop_
            ->Post([&] {
                VLOG(1) << "Client sending mid-handoff data";
                client->Send(mid_handoff_data.data(), mid_handoff_data.size()).IgnoreError();
            })
            .IgnoreError();

    // 6. Verify all data is received on the target loop.
    ASSERT_TRUE(data_received_on_target.WaitForNotificationWithTimeout(absl::Seconds(5)));
    EXPECT_EQ(total_received_data, expected_data);

    // Clean up.
    if (server_side_client) {
        auto loop = server_side_client->GetLoop();
        loop->Post([s = server_side_client] { s->Close(); }).IgnoreError();
    }
    client_closed.WaitForNotification();
}

TEST_F(LoopHandoffTest, DataTransferAfterHandoff) {
    absl::Notification received_ping;
    absl::Notification received_pong;
    std::atomic<std::thread::id> server_read_thread_id;
    const std::string ping = "ping";
    const std::string pong = "pong";

    auto target_loop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
    AsyncSocketServer::LoopProvider loop_provider = [&](const network::Endpoint&) {
        return target_loop.get();
    };

    std::shared_ptr<AsyncSocket> server_side_client;
    absl::Notification client_closed;
    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        const std::weak_ptr<AsyncSocket> weak_socket(socket);

        socket->SetOnCloseCallback([&] { client_closed.Notify(); });
        socket->SetOnReadCallbackNoFlowControl(
                [&, s = weak_socket](std::string_view data, absl::Status) {
                    if (const auto ss = s.lock()) {
                        if (data == ping) {
                            server_read_thread_id = std::this_thread::get_id();
                            received_ping.Notify();
                            ss->Send(pong.data(), pong.size()).IgnoreError();
                        }
                    }
                });
        server_side_client = std::move(socket);
        return true;
    };

    ScopedAsyncServer server(server_loop_
                                     ->PostAndWait([&] {
                                         auto endpoint =
                                                 ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
                                         return factory_->CreateServer(server_loop_.get(), endpoint,
                                                                       on_connect, loop_provider);
                                     })
                                     .value());

    auto server_endpoint = server_loop_->PostAndWait([&] { return server->GetEndpoint(); }).value();

    ScopedAsyncSocket client(server_loop_
                                     ->PostAndWait([&] {
                                         return factory_->CreateSocket(server_loop_.get(),
                                                                       server_endpoint);
                                     })
                                     .value());

    server_loop_
            ->Post([&] {
                client->SetOnConnectedCallback([&](AsyncSocket& s, absl::Status err) {
                    EXPECT_THAT(err, IsOk());
                    s.SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status) {
                        if (data == pong) {
                            received_pong.Notify();
                        }
                    });
                    s.Send(ping.data(), ping.size()).IgnoreError();
                });
                client->Connect().IgnoreError();
            })
            .IgnoreError();

    received_ping.WaitForNotification();
    received_pong.WaitForNotification();

    // Verify server read happened on the target loop's thread.
    EXPECT_NE(server_read_thread_id.load(), server_thread_.get_id());

    if (server_side_client) {
        auto loop = server_side_client->GetLoop();
        loop->Post([s = server_side_client] { s->Close(); }).IgnoreError();
    }
    client_closed.WaitForNotification();
}

TEST_F(LoopHandoffTest, HandoffToSharedPool) {
    const int kPoolSize = 2;
    std::vector<std::unique_ptr<ThreadedEventLoop>> pool;
    for (int i = 0; i < kPoolSize; ++i) {
        pool.push_back(ThreadedEventLoop::Create(LibuvEventLoop::Create()));
    }

    std::atomic<int> next_loop{0};
    AsyncSocketServer::LoopProvider loop_provider = [&](const network::Endpoint&) {
        return pool[next_loop++ % kPoolSize].get();
    };

    std::atomic<int> connections_count{0};
    absl::Notification all_connected;
    absl::BlockingCounter client_closed(kPoolSize * 2);
    std::vector<std::shared_ptr<AsyncSocket>> server_side_clients;
    std::mutex clients_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        VLOG(1) << "HandoffToSharedPool: on_connect triggered for socket=" << socket.get();
        socket->SetOnCloseCallback([&] { client_closed.DecrementCount(); });
        socket->SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            server_side_clients.push_back(socket);
        }
        if (++connections_count == kPoolSize * 2) {
            all_connected.Notify();
        }
        return true;
    };

    ScopedAsyncServer server(server_loop_
                                     ->PostAndWait([&] {
                                         auto endpoint =
                                                 ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
                                         return factory_->CreateServer(server_loop_.get(), endpoint,
                                                                       on_connect, loop_provider);
                                     })
                                     .value());

    auto server_endpoint = server_loop_->PostAndWait([&] { return server->GetEndpoint(); }).value();

    std::vector<ScopedAsyncSocket> clients;
    for (int i = 0; i < kPoolSize * 2; ++i) {
        clients.emplace_back(server_loop_
                                     ->PostAndWait([&] {
                                         return factory_->CreateSocket(server_loop_.get(),
                                                                       server_endpoint);
                                     })
                                     .value());

        auto& client = clients.back();
        server_loop_
                ->Post([&] {
                    client->SetOnConnectedCallback([](AsyncSocket& s, absl::Status err) {
                        EXPECT_THAT(err, IsOk());
                        s.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
                    });
                    client->Connect().IgnoreError();
                })
                .IgnoreError();
    }

    all_connected.WaitForNotification();
    EXPECT_EQ(connections_count.load(), kPoolSize * 2);

    // Verify that connections are distributed across the pool.
    std::set<EventLoop*> loops_used;
    for (auto& s : server_side_clients) {
        loops_used.insert(s->GetLoop());
    }
    EXPECT_EQ(loops_used.size(), kPoolSize);

    for (auto& s : server_side_clients) {
        auto loop = s->GetLoop();
        loop->Post([s] { s->Close(); }).IgnoreError();
    }
    client_closed.Wait();

    server_loop_->PostAndWait([&] { return server->Close(); }).IgnoreError();
}

TEST_F(LoopHandoffTest, LegacyCreateServerMethod) {
    absl::Notification connected;
    absl::Notification client_closed;
    std::atomic<std::thread::id> target_thread_id;
    std::shared_ptr<AsyncSocket> server_side_client;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        target_thread_id = std::this_thread::get_id();
        socket->SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
        socket->SetOnCloseCallback([&] { client_closed.Notify(); });
        server_side_client = std::move(socket);
        connected.Notify();
        return true;
    };

    // Use the 3-argument version of CreateServer.
    ScopedAsyncServer server(server_loop_
                                     ->PostAndWait([&] {
                                         auto endpoint =
                                                 ToEndpoint(ToIpAddress("127.0.0.1").value(), 0);
                                         return factory_->CreateServer(server_loop_.get(), endpoint,
                                                                       std::move(on_connect));
                                     })
                                     .value());

    auto server_endpoint = server_loop_->PostAndWait([&] { return server->GetEndpoint(); }).value();

    ScopedAsyncSocket client(server_loop_
                                     ->PostAndWait([&] {
                                         return factory_->CreateSocket(server_loop_.get(),
                                                                       server_endpoint);
                                     })
                                     .value());

    server_loop_
            ->Post([&] {
                client->SetOnConnectedCallback([](AsyncSocket& s, absl::Status err) {
                    EXPECT_THAT(err, IsOk());
                    s.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
                });
                client->Connect().IgnoreError();
            })
            .IgnoreError();

    connected.WaitForNotification();
    // Verify it ran on the server's loop thread.
    EXPECT_EQ(target_thread_id.load(), server_thread_.get_id());

    if (server_side_client) {
        auto loop = server_side_client->GetLoop();
        loop->Post([s = server_side_client] { s->Close(); }).IgnoreError();
    }
    client_closed.WaitForNotification();
}

}  // namespace
}  // namespace goldfish::async
