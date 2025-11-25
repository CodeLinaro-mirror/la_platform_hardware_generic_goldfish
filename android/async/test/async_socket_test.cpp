#include "goldfish/async/async_socket.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/network/dns_resolver.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::async {
using namespace std::chrono_literals;
using absl_testing::IsOk;
using network::Endpoint;

// =================================================================
//                      TEST FIXTURE
// =================================================================

class AsyncSocketTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mEventLoop = LibuvEventLoop::create();
        mRawEventLoop = mEventLoop.get();
        mFactory = std::make_unique<LibuvAsyncSocketFactory>();
        mLoopThread = std::thread([this] { (void)mRawEventLoop->run(); });
        // Wait for the loop to actually start.
        while (mRawEventLoop->getState() != LooperStatusEvent::State::RUNNING) {
            std::this_thread::sleep_for(10ms);
        }
    }

    void TearDown() override {
        auto s = mRawEventLoop->shutdownAndWait(100ms);
        ASSERT_THAT(s, absl_testing::IsOk());
        if (mLoopThread.joinable()) {
            mLoopThread.join();
        }
    }

    template <typename T>
    void runUntil(std::future<T>& future,
                  std::chrono::milliseconds timeout = 2s) {
        ASSERT_EQ(future.wait_for(timeout), std::future_status::ready);
    }

    void runUntil(std::future<void>& future) {
        ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    }

    template <typename F>
    auto postAndWait(F&& func) -> decltype(func()) {
        auto res = mRawEventLoop->postAndWait(std::forward<F>(func));
        EXPECT_THAT(res, absl_testing::IsOk());
        if constexpr (std::is_void_v<decltype(func())>) {
            return;
        } else {
            return *res;
        }
    }

    std::unique_ptr<LibuvEventLoop> mEventLoop;
    LibuvEventLoop* mRawEventLoop;
    std::shared_ptr<AsyncSocketFactory> mFactory;
    std::thread mLoopThread;
};

// =================================================================
//                      UPDATED TESTS
// =================================================================

TEST_F(AsyncSocketTest, ConnectAndClose) {
    std::promise<void> connected_promise;
    auto connected_future = connected_promise.get_future();
    std::promise<void> client_closed_promise;
    auto client_closed_future = client_closed_promise.get_future();

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallbackNoFlowControl([](std::string_view data, absl::Status err) {});

        connected_promise.set_value();
        LOG(INFO) << "Server received connection, closing incoming.";
        // Let's be alive a bit so we don't get crazy concurrency.
        std::this_thread::sleep_for(10ms);
        socket->close();
        return true;
    };

    ScopedAsyncServer server(postAndWait([&] {
        auto endpoint = Endpoint::create("127.0.0.1", 0).value();
        return mFactory->createServer(mRawEventLoop, endpoint, on_connect);
    }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        auto endpoint = Endpoint::create("127.0.0.1", port).value();
        return mFactory->createSocket(mRawEventLoop, endpoint);
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([](AsyncSocket& socket, absl::Status err) {
            LOG(INFO) << err;
            socket.setOnReadCallbackNoFlowControl([](std::string_view data, absl::Status err) {});
        });
        client->setOnCloseCallback([&] {
            LOG(INFO) << "Client is closed";
            client_closed_promise.set_value();
        });
        ASSERT_THAT(client->connect(), IsOk());
    });

    runUntil(connected_future);
    runUntil(client_closed_future);
}

TEST_F(AsyncSocketTest, ClientCanSendData) {
    const std::string sent_message = "Hello, from the client!";
    std::promise<std::string> received_promise;
    auto received_future = received_promise.get_future();
    std::promise<void> closed_promise;
    auto closed_future = closed_promise.get_future();

    // We need to store the server-side socket, a Scoped wrapper in a vector is
    // perfect.
    std::vector<ScopedAsyncSocket> server_sockets;
    std::mutex server_sockets_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) received_promise.set_value(std::string(data));
        });

        std::lock_guard<std::mutex> lock(server_sockets_mutex);
        server_sockets.emplace_back(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait([&] {
        auto endpoint = Endpoint::create("127.0.0.1", 0).value();
        return mFactory->createServer(mRawEventLoop, endpoint, on_connect);
    }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        auto endpoint = Endpoint::create("127.0.0.1", port).value();
        return mFactory->createSocket(mRawEventLoop, endpoint);
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnCloseCallback([&] { closed_promise.set_value(); });
        client->setOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
            ASSERT_EQ(&socket, client.get());

            client->setOnReadCallbackNoFlowControl([](std::string_view data, absl::Status err) {});

            ASSERT_THAT(client->send(sent_message.data(), sent_message.size(),
                                     [&](auto) { client->close(); }),
                        IsOk());
        });
        ASSERT_THAT(client->connect(), IsOk());
    });

    runUntil(received_future);
    EXPECT_EQ(received_future.get(), sent_message);

    runUntil(closed_future);
}

TEST_F(AsyncSocketTest, EchoTest) {
    const std::string original_message = "Ping";
    std::promise<std::string> echo_promise;
    auto echo_future = echo_promise.get_future();
    std::vector<ScopedAsyncSocket> server_sockets;
    std::mutex server_sockets_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallbackNoFlowControl(
                [sock = socket.get()](std::string_view data, absl::Status err) {
                    ASSERT_THAT(sock->send(data.data(), data.size()), IsOk());
                });
        // Keep the socket alive by moving it into the scoped vector
        std::lock_guard<std::mutex> lock(server_sockets_mutex);
        server_sockets.emplace_back(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait([&] {
        auto endpoint = Endpoint::create("127.0.0.1", 0).value();
        return mFactory->createServer(mRawEventLoop, endpoint, on_connect);
    }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        auto endpoint = Endpoint::create("127.0.0.1", port).value();
        return mFactory->createSocket(mRawEventLoop, endpoint);
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            echo_promise.set_value(std::string(data));
        });
        client->setOnConnectedCallback([&original_message, &echo_promise](AsyncSocket& socket,
                                                                          absl::Status err) {
            socket.setOnReadCallbackNoFlowControl(
                    [&echo_promise](std::string_view data, absl::Status err) {
                        echo_promise.set_value(std::string(data));
                    });

            ASSERT_THAT(socket.send(original_message.data(), original_message.size()), IsOk());
        });
        ASSERT_THAT(client->connect(), IsOk());
    });

    runUntil(echo_future);
    EXPECT_EQ(echo_future.get(), original_message);
}

TEST_F(AsyncSocketTest, LargeDataTransfer) {
    std::string large_message;
    large_message.reserve(5 * 1024 * 1024);
    for (int i = 0; i < (5 * 1024 * 1024) / 10; ++i)
        large_message.append("0123456789");

    ScopedAsyncSocket server_socket;  // Will hold the server-side socket
    std::promise<size_t> received_size_promise;
    auto received_size_future = received_size_promise.get_future();
    std::stringstream received_data;
    std::mutex received_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            std::lock_guard<std::mutex> lock(received_mutex);
            received_data << data;
        });
        socket->setOnCloseCallback([&] {
            received_size_promise.set_value(received_data.str().size());
        });

        // Assign to the Scoped wrapper in the outer scope to manage lifetime
        server_socket = ScopedAsyncSocket(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait([&] {
        auto endpoint = Endpoint::create("127.0.0.1", 0).value();
        return mFactory->createServer(mRawEventLoop, endpoint, on_connect);
    }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        auto endpoint = Endpoint::create("127.0.0.1", port).value();
        return mFactory->createSocket(mRawEventLoop, endpoint);
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
            ASSERT_EQ(&socket, client.get());

            client->setOnReadCallbackNoFlowControl([](std::string_view data, absl::Status err) {});

            ASSERT_THAT(client->send(large_message.data(), large_message.size(),
                                     [&](auto) { client->close(); }),
                        IsOk());
        });
        ASSERT_THAT(client->connect(), IsOk());
    });

    // Our build servers are under pretty heavy load running all the tests
    // and The qemu message pump is very slow, so we give it extra time.
    runUntil(received_size_future, 30s);
    EXPECT_EQ(received_size_future.get(), large_message.size());
}

TEST_F(AsyncSocketTest, MultiThreadedSendIsSafe) {
    const std::string message_per_thread =
            "This is a message from one of many threads. ";
    const int num_threads = 10;
    std::promise<size_t> received_size_promise;
    auto received_size_future = received_size_promise.get_future();
    std::stringstream received_data;
    std::mutex received_mutex;
    ScopedAsyncSocket server_socket;  // To hold the server-side socket

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            std::lock_guard<std::mutex> lock(received_mutex);
            received_data << data;
        });
        socket->setOnCloseCallback([&] {
            received_size_promise.set_value(received_data.str().size());
        });
        server_socket = ScopedAsyncSocket(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait([&] {
        auto endpoint = Endpoint::create("127.0.0.1", 0).value();
        return mFactory->createServer(mRawEventLoop, endpoint, on_connect);
    }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        auto endpoint = Endpoint::create("127.0.0.1", port).value();
        return mFactory->createSocket(mRawEventLoop, endpoint);
    }));
    ASSERT_NE(client, nullptr);

    std::vector<std::thread> threads;
    absl::Notification connected;
    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([&connected](AsyncSocket& socket, absl::Status err) {
            socket.setOnReadCallbackNoFlowControl([](std::string_view data, absl::Status err) {});
            connected.Notify();
        });
        ASSERT_THAT(client->connect(), IsOk());
    });

    connected.WaitForNotification();

    // Note: i is read in VLOG(1) below so it is read in thread
    // and written on main.
    std::atomic<int> i = 0;
    for (; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            absl::Notification bytesAway;
            mRawEventLoop->post([&]() {
                VLOG(1) << "Sending data from thread: " << i;
                ASSERT_THAT(client->send(message_per_thread.data(),
                                         message_per_thread.size(),
                                         [&](auto) { bytesAway.Notify(); }),
                            IsOk());
            });
            bytesAway.WaitForNotificationWithTimeout(absl::Milliseconds(100));
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // We still need to close the client to trigger the server's OnCloseCallback
    // which fulfills the promise for this test. The Scoped wrapper will also
    // post a close, but that's harmless (close is idempotent).
    mRawEventLoop->post([&] { client->close(); });

    runUntil(received_size_future);
    EXPECT_EQ(received_size_future.get(),
              num_threads * message_per_thread.size());
}

TEST_F(AsyncSocketTest, ConnectAndCloseWithHostname) {
    std::promise<void> connected_promise;
    auto connected_future = connected_promise.get_future();
    std::promise<void> client_closed_promise;
    auto client_closed_future = client_closed_promise.get_future();

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallbackNoFlowControl([](std::string_view data, absl::Status err) {});
        connected_promise.set_value();
        LOG(INFO) << "Server received connection, closing incoming.";
        // Let's be alive a bit so we don't get crazy concurrency.
        std::this_thread::sleep_for(10ms);
        socket->close();
        return true;
    };

    ScopedAsyncServer server(postAndWait([&] {
        return createServerFromHostname(*mFactory, mRawEventLoop, "localhost:0", on_connect);
    }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return createSocketFromHostname(*mFactory, mRawEventLoop,
                                        "localhost:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([](AsyncSocket& socket, absl::Status err) {
            socket.setOnReadCallbackNoFlowControl([](std::string_view data, absl::Status err) {});
            LOG(INFO) << err;
        });
        client->setOnCloseCallback([&] {
            LOG(INFO) << "Client is closed";
            client_closed_promise.set_value();
        });
        ASSERT_THAT(client->connect(), IsOk());
    });

    runUntil(connected_future);
    runUntil(client_closed_future);
}

TEST_F(AsyncSocketTest, ConnectAndCloseWithABadHostname) {
    std::promise<void> connected_promise;
    auto connected_future = connected_promise.get_future();
    std::promise<void> client_closed_promise;
    auto client_closed_future = client_closed_promise.get_future();

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        connected_promise.set_value();
        LOG(INFO) << "Server received connection, closing incoming.";
        // Let's be alive a bit so we don't get crazy concurrency.
        std::this_thread::sleep_for(10ms);
        socket->close();
        return true;
    };

    ScopedAsyncServer server(postAndWait([&] {
        return createServerFromHostname(*mFactory, mRawEventLoop, "wanou_localhost:0", on_connect);
    }));
    ASSERT_EQ(server, nullptr);
}

TEST_F(AsyncSocketTest, EchoTestWithHostname) {
    const std::string original_message = "Ping";
    std::promise<std::string> echo_promise;
    auto echo_future = echo_promise.get_future();
    std::vector<ScopedAsyncSocket> server_sockets;
    std::mutex server_sockets_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallbackNoFlowControl(
                [sock = socket.get()](std::string_view data, absl::Status err) {
                    ASSERT_THAT(sock->send(data.data(), data.size()), IsOk());
                });
        // Keep the socket alive by moving it into the scoped vector
        std::lock_guard<std::mutex> lock(server_sockets_mutex);
        server_sockets.emplace_back(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait([&] {
        return createServerFromHostname(*mFactory, mRawEventLoop, "localhost:0", on_connect);
    }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return createSocketFromHostname(*mFactory, mRawEventLoop,
                                        "localhost:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([&](AsyncSocket& socket, absl::Status err) {
            ASSERT_EQ(&socket, client.get());

            client->setOnReadCallbackNoFlowControl(
                    [&echo_promise](std::string_view data, absl::Status err) {
                        echo_promise.set_value(std::string(data));
                    });

            ASSERT_THAT(client->send(original_message.data(), original_message.size()), IsOk());
        });
        ASSERT_THAT(client->connect(), IsOk());
    });

    runUntil(echo_future);
    EXPECT_EQ(echo_future.get(), original_message);
}

}  // namespace goldfish::async
