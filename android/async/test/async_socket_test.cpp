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

#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/async_socket_utils.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"

namespace goldfish::async {
using namespace std::chrono_literals;

// =================================================================
//                      TEST FIXTURE
// =================================================================

class AsyncSocketTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mLoop = std::make_unique<LibuvEventLoop>();
        mFactory = std::make_unique<LibuvAsyncSocketFactory>();
        mLoopThread = std::thread([this] { mLoop->run(); });
    }

    void TearDown() override {
        mLoop->stop();
        if (mLoopThread.joinable()) {
            mLoopThread.join();
        }
    }

    // Helper function to get a value from the loop thread, assuming EventLoop
    // has the postAndWait helper we discussed.
    template <typename F>
    auto postAndWait(F&& func) {
        // If postAndWait is not yet implemented, this logic can be done
        // manually with a std::promise for each call.
        return mLoop->postAndWait(std::forward<F>(func));
    }

    std::unique_ptr<EventLoop> mLoop;
    std::shared_ptr<AsyncSocketFactory> mFactory;
    std::thread mLoopThread;
};

// =================================================================
//                      UPDATED TESTS
// =================================================================

TEST_F(AsyncSocketTest, ConnectAndClose) {
    std::promise<void> connected_promise;
    std::promise<void> client_closed_promise;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        connected_promise.set_value();
        socket->close();
        return true;
    };

    ScopedAsyncServer server(postAndWait(
            [&] { return mFactory->createServer(mLoop.get(), "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mLoop.get(), "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mLoop->post([&]() {
        client->setOnConnectedCallback([&](absl::Status err) { LOG(INFO) << err; });
        client->setOnCloseCallback([&] { client_closed_promise.set_value(); });
        ASSERT_TRUE(client->connect().ok());
    });

    ASSERT_EQ(connected_promise.get_future().wait_for(1s), std::future_status::ready);
    ASSERT_EQ(client_closed_promise.get_future().wait_for(1s), std::future_status::ready);
}

TEST_F(AsyncSocketTest, ClientCanSendData) {
    const std::string sent_message = "Hello, from the client!";
    std::promise<std::string> received_promise;
    std::promise<void> closed_promise;

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
            [&] { return mFactory->createServer(mLoop.get(), "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mLoop.get(), "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mLoop->post([&]() {
        client->setOnCloseCallback([&] { closed_promise.set_value(); });
        client->setOnConnectedCallback([&](auto) {
            client->send(sent_message.data(), sent_message.size(), [&](auto) { client->close(); });
        });
        ASSERT_TRUE(client->connect().ok());
    });

    auto future = received_promise.get_future();
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(future.get(), sent_message);

    ASSERT_EQ(closed_promise.get_future().wait_for(1s), std::future_status::ready);
}

TEST_F(AsyncSocketTest, EchoTest) {
    const std::string original_message = "Ping";
    std::promise<std::string> echo_promise;
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
            [&] { return mFactory->createServer(mLoop.get(), "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mLoop.get(), "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mLoop->post([&]() {
        client->setOnReadCallback([&](std::string_view data, absl::Status err) {
            echo_promise.set_value(std::string(data));
        });
        client->setOnConnectedCallback(
                [&](auto) { client->send(original_message.data(), original_message.size()); });
        ASSERT_TRUE(client->connect().ok());
    });

    auto future = echo_promise.get_future();
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(future.get(), original_message);
}

TEST_F(AsyncSocketTest, LargeDataTransfer) {
    std::string large_message;
    large_message.reserve(5 * 1024 * 1024);
    for (int i = 0; i < (5 * 1024 * 1024) / 10; ++i) large_message.append("0123456789");

    ScopedAsyncSocket server_socket;  // Will hold the server-side socket
    std::promise<size_t> received_size_promise;
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
            [&] { return mFactory->createServer(mLoop.get(), "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mLoop.get(), "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    mLoop->post([&]() {
        client->setOnConnectedCallback([&](auto) {
            client->send(large_message.data(), large_message.size(),
                         [&](auto) { client->close(); });
        });
        ASSERT_TRUE(client->connect().ok());
    });

    auto future = received_size_promise.get_future();
    ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(future.get(), large_message.size());
}

TEST_F(AsyncSocketTest, MultiThreadedSendIsSafe) {
    const std::string message_per_thread = "This is a message from one of many threads. ";
    const int num_threads = 10;
    std::promise<size_t> received_size_promise;
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
            [&] { return mFactory->createServer(mLoop.get(), "127.0.0.1:0", on_connect); }));
    ASSERT_NE(server, nullptr);
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(postAndWait([&, port] {
        return mFactory->createSocket(mLoop.get(), "127.0.0.1:" + std::to_string(port));
    }));
    ASSERT_NE(client, nullptr);

    std::vector<std::thread> threads;
    absl::Notification connected;
    mLoop->post([&]() {
        client->setOnConnectedCallback([&](auto) { connected.Notify(); });
        ASSERT_TRUE(client->connect().ok());
    });

    connected.WaitForNotification();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            mLoop->post(
                    [&]() { client->send(message_per_thread.data(), message_per_thread.size()); });
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // We still need to close the client to trigger the server's OnCloseCallback
    // which fulfills the promise for this test. The Scoped wrapper will also
    // post a close, but that's harmless (close is idempotent).
    mLoop->post([&] { client->close(); });

    auto future = received_size_promise.get_future();
    ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(future.get(), num_threads * message_per_thread.size());
}

TEST_F(AsyncSocketTest, SendSynchronouslyBlocksAndSucceeds) {
    const std::string message_to_send = "blocking send";
    std::promise<std::string> received_promise;

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
    ScopedAsyncServer server(mLoop->postAndWait(
            [&] { return mFactory->createServer(mLoop.get(), "127.0.0.1:0", on_connect); }));
    int port = postAndWait([&] { return server->port(); });

    ScopedAsyncSocket client(mLoop->postAndWait([&] {
        return mFactory->createSocket(mLoop.get(), "127.0.0.1:" + std::to_string(port));
    }));

    absl::Notification connected_notification;
    mLoop->post([&]() {
        client->setOnConnectedCallback([&](auto) { connected_notification.Notify(); });
        client->connect();
    });
    connected_notification.WaitForNotification();

    // --- Execute: Call the blocking function from the main test thread ---
    absl::Status status = sendSynchronously(client.get(), message_to_send);

    ASSERT_TRUE(status.ok());

    auto future = received_promise.get_future();
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(future.get(), message_to_send);
}

}  // namespace goldfish::async