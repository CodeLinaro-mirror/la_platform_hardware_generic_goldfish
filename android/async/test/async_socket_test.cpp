#include "goldfish/async/async_socket.h"

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "fake_qemu_callbacks.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/async_socket_utils.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/qemu_socket_factory.h"

namespace goldfish::async {
using namespace std::chrono_literals;

// =================================================================
//                      TEST FIXTURE
// =================================================================

class AsyncSocketTest : public ::testing::TestWithParam<std::string> {
  protected:
    void SetUp() override {
        mLoopType = GetParam();
        if (mLoopType == "libuv") {
            mEventLoop = LibuvEventLoop::create();
            mRawEventLoop = mEventLoop.get();
            mFactory = std::make_unique<LibuvAsyncSocketFactory>();
            mLoopThread = std::thread([this] { mRawEventLoop->run(); });
        } else if (mLoopType == "qemu") {
            absl::Notification running;
            mEventLoop = QemuEventLoop::create();
            mRawEventLoop = mEventLoop.get();
            mFactory = std::make_unique<QemuSocketFactory>();
            mStopQemuLooper = false;
            mQemuLooperThread = std::thread([&, this] {
                fake_qemu_start_io_loop();
                running.Notify();
                while (!mStopQemuLooper) {
                    fake_qemu_advance_ms(1);
                    std::this_thread::sleep_for(1ms);
                }
                fake_qemu_stop_io_loop();
            });
            running.WaitForNotification();
        }
    }

    void TearDown() override {
        if (mLoopType == "libuv") {
            auto shutdown_future = mRawEventLoop->shutdown(100ms);
            ASSERT_EQ(shutdown_future.wait_for(2s), std::future_status::ready);
            mRawEventLoop->stop();
            if (mLoopThread.joinable()) {
                mLoopThread.join();
            }
        } else if (mLoopType == "qemu") {
            mStopQemuLooper = true;
            if (mQemuLooperThread.joinable()) {
                mQemuLooperThread.join();
            }
            fake_qemu_reset();
        }
    }

    template <typename T>
    void runUntil(std::future<T>& future) {
        ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    }

    void runUntil(std::future<void>& future) {
        ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    }

    template <typename F>
    auto postAndWait(F&& func) {
        return mRawEventLoop->postAndWait(std::forward<F>(func));
    }

    std::string mLoopType;
    std::unique_ptr<EventLoop> mEventLoop;
    EventLoop* mRawEventLoop;
    std::shared_ptr<AsyncSocketFactory> mFactory;
    std::thread mLoopThread;
    std::thread mQemuLooperThread;
    std::atomic<bool> mStopQemuLooper{false};
};

// =================================================================
//                      UPDATED TESTS
// =================================================================

TEST_P(AsyncSocketTest, ConnectAndClose) {
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

    ScopedAsyncServer server(postAndWait(
            [&] { return mFactory->createServer(mRawEventLoop, "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mRawEventLoop, "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([&](absl::Status err) { LOG(INFO) << err; });
        client->setOnCloseCallback([&] {
            LOG(INFO) << "Client is closed";
            client_closed_promise.set_value();
        });
        ASSERT_TRUE(client->connect().ok());
    });

    runUntil(connected_future);
    runUntil(client_closed_future);
}

TEST_P(AsyncSocketTest, ClientCanSendData) {
    const std::string sent_message = "Hello, from the client!";
    std::promise<std::string> received_promise;
    auto received_future = received_promise.get_future();
    std::promise<void> closed_promise;
    auto closed_future = closed_promise.get_future();

    // We need to store the server-side socket, a Scoped wrapper in a vector is perfect.
    std::vector<ScopedAsyncSocket> server_sockets;
    std::mutex server_sockets_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallback([&](std::string_view data, absl::Status err) {
            if (err.ok()) received_promise.set_value(std::string(data));
        });

        std::lock_guard<std::mutex> lock(server_sockets_mutex);
        server_sockets.emplace_back(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait(
            [&] { return mFactory->createServer(mRawEventLoop, "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mRawEventLoop, "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnCloseCallback([&] { closed_promise.set_value(); });
        client->setOnConnectedCallback([&](auto) {
            client->send(sent_message.data(), sent_message.size(), [&](auto) { client->close(); });
        });
        ASSERT_TRUE(client->connect().ok());
    });

    runUntil(received_future);
    EXPECT_EQ(received_future.get(), sent_message);

    runUntil(closed_future);
}

TEST_P(AsyncSocketTest, EchoTest) {
    const std::string original_message = "Ping";
    std::promise<std::string> echo_promise;
    auto echo_future = echo_promise.get_future();
    std::vector<ScopedAsyncSocket> server_sockets;
    std::mutex server_sockets_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallback([sock = socket.get()](std::string_view data, absl::Status err) {
            sock->send(data.data(), data.size());
        });
        // Keep the socket alive by moving it into the scoped vector
        std::lock_guard<std::mutex> lock(server_sockets_mutex);
        server_sockets.emplace_back(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait(
            [&] { return mFactory->createServer(mRawEventLoop, "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mRawEventLoop, "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnReadCallback([&](std::string_view data, absl::Status err) {
            echo_promise.set_value(std::string(data));
        });
        client->setOnConnectedCallback(
                [&](auto) { client->send(original_message.data(), original_message.size()); });
        ASSERT_TRUE(client->connect().ok());
    });

    runUntil(echo_future);
    EXPECT_EQ(echo_future.get(), original_message);
}

TEST_P(AsyncSocketTest, LargeDataTransfer) {
    std::string large_message;
    large_message.reserve(5 * 1024 * 1024);
    for (int i = 0; i < (5 * 1024 * 1024) / 10; ++i) large_message.append("0123456789");

    ScopedAsyncSocket server_socket;  // Will hold the server-side socket
    std::promise<size_t> received_size_promise;
    auto received_size_future = received_size_promise.get_future();
    std::stringstream received_data;
    std::mutex received_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallback([&](std::string_view data, absl::Status err) {
            std::lock_guard<std::mutex> lock(received_mutex);
            received_data << data;
        });
        socket->setOnCloseCallback(
                [&] { received_size_promise.set_value(received_data.str().size()); });

        // Assign to the Scoped wrapper in the outer scope to manage lifetime
        server_socket = ScopedAsyncSocket(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait(
            [&] { return mFactory->createServer(mRawEventLoop, "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mRawEventLoop, "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([&](auto) {
            client->send(large_message.data(), large_message.size(),
                         [&](auto) { client->close(); });
        });
        ASSERT_TRUE(client->connect().ok());
    });

    runUntil(received_size_future);
    EXPECT_EQ(received_size_future.get(), large_message.size());
}

TEST_P(AsyncSocketTest, MultiThreadedSendIsSafe) {
    const std::string message_per_thread = "This is a message from one of many threads. ";
    const int num_threads = 10;
    std::promise<size_t> received_size_promise;
    auto received_size_future = received_size_promise.get_future();
    std::stringstream received_data;
    std::mutex received_mutex;
    ScopedAsyncSocket server_socket;  // To hold the server-side socket

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->setOnReadCallback([&](std::string_view data, absl::Status err) {
            std::lock_guard<std::mutex> lock(received_mutex);
            received_data << data;
        });
        socket->setOnCloseCallback(
                [&] { received_size_promise.set_value(received_data.str().size()); });
        server_socket = ScopedAsyncSocket(std::move(socket));
        return true;
    };

    ScopedAsyncServer server(postAndWait(
            [&] { return mFactory->createServer(mRawEventLoop, "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mRawEventLoop, "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    std::vector<std::thread> threads;
    absl::Notification connected;
    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([&](auto) { connected.Notify(); });
        ASSERT_TRUE(client->connect().ok());
    });

    connected.WaitForNotification();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            absl::Notification bytesAway;
            mRawEventLoop->post([&]() {
                VLOG(1) << "Sending data from thread: " << i;
                client->send(message_per_thread.data(), message_per_thread.size(),
                             [&](auto) { bytesAway.Notify(); });
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
    EXPECT_EQ(received_size_future.get(), num_threads * message_per_thread.size());
}

TEST_P(AsyncSocketTest, SendSynchronouslyBlocksAndSucceeds) {
    const std::string message_to_send = "blocking send";
    std::promise<std::string> received_promise;
    auto received_future = received_promise.get_future();

    // This will hold the server-side connection to keep it alive.
    ScopedAsyncSocket server_connection;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> accepted_socket) {
        // The server sets a read callback to fulfill the promise when data arrives.
        accepted_socket->setOnReadCallback([&](std::string_view data, absl::Status err) {
            if (err.ok()) received_promise.set_value(std::string(data));
        });
        // Take ownership of the accepted socket.
        server_connection = ScopedAsyncSocket(std::move(accepted_socket));
        return true;
    };

    // --- Setup: Create server, client, and establish a connection ---
    ScopedAsyncServer server(mRawEventLoop->postAndWait(
            [&] { return mFactory->createServer(mRawEventLoop, "127.0.0.1:0", on_connect); }));
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(mRawEventLoop->postAndWait([&] {
        return mFactory->createSocket(mRawEventLoop, "127.0.0.1:" + std::to_string(port));
    }));

    absl::Notification connected_notification;
    mRawEventLoop->post([&]() {
        client->setOnConnectedCallback([&](auto) { connected_notification.Notify(); });
        client->connect();
    });
    connected_notification.WaitForNotification();

    // --- Execute: Call the blocking function from the main test thread ---
    absl::Status status = sendSynchronously(client.get(), message_to_send);

    ASSERT_TRUE(status.ok());

    runUntil(received_future);
    EXPECT_EQ(received_future.get(), message_to_send);
}

INSTANTIATE_TEST_SUITE_P(SocketImplementations, AsyncSocketTest,
#ifdef _WIN32
                         // We do not have qemu fake drivers for windows so we will not be running
                         // these tests.
                         ::testing::Values("libuv"),
#else
                         ::testing::Values("libuv", "qemu"),
#endif
                         [](const ::testing::TestParamInfo<AsyncSocketTest::ParamType>& info) {
                             return info.param;
                         });
}  // namespace goldfish::async
