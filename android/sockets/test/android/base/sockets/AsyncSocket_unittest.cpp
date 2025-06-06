// Copyright 2025 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "absl/synchronization/notification.h"

#include "android/base/sockets/SimpleTestServer.h"
#include "android/base/system/storage_capacity.h"

namespace android::base::testing {

using android::base::StorageCapacity;
using namespace std::chrono_literals;
using namespace std::literals::string_view_literals;

// A helper function to create a large message for testing.
std::string createLargeMessage(std::string message, int repetitions) {
    for (; repetitions > 0; repetitions--) {
        message += message;
    }
    return message;
}

class AsyncSocketTest : public ::testing::Test {
  protected:
    void SetUp() override {}

    void TearDown() override {
        // Ensure the server is fully shut down after each test.
        if (mServer) {
            mServer->waitUntilCompleted();
        }
    }

    // Helper method to run a standard test case.
    // It handles server start, socket connection, waiting for close, and verification.
    void runTest(
            std::function<void(std::shared_ptr<ChecksumSocket>)> testLogic,
            ReplyStrategy replyCallback = [](auto _) { return ""; }) {
        mServer = std::make_unique<SimpleTestServer>(0, replyCallback);
        absl::Notification socketClosed;

        mServer->start();
        auto socket = mServer->connect(ChecksumSocket::Mode::StoreAll);
        socket->setOnCloseCallback([&socketClosed] { socketClosed.Notify(); });

        // Execute the custom logic for the specific test.
        testLogic(socket);
        socket->end();

        // Wait for the socket to close, this means all data should have been received.
        ASSERT_TRUE(socketClosed.WaitForNotificationWithTimeout(absl::Seconds(2)))
                << "Test timed out waiting for socket to close.";

        verifySocketCommunication(socket);
    }

    // Verify that messages arrived at server and vice versa.
    void verifySocketCommunication(std::shared_ptr<ChecksumSocket> socket) {
        auto [client_send, server_recv] = socket->sendStrings();
        auto [client_recv, server_send] = socket->recvStrings();

        EXPECT_EQ(client_send, server_recv)
                << "Data sent by the client was not correctly received by the server.";
        EXPECT_EQ(client_recv, server_send)
                << "Data sent by the server was not correctly received by the client.";
    }

    std::unique_ptr<SimpleTestServer> mServer;
};

TEST_F(AsyncSocketTest, CanSendMessage) {
    runTest([](auto socket) { socket->send("Hello World"sv); });
}

TEST_F(AsyncSocketTest, MessagesArriveInOrder) {
    // Note if messages don't arrive in order we expect the common
    // send == receive assertion to fail.
    runTest([](auto socket) {
        for (int i = 0; i < 1024; i++) {
            socket->send("Hello"sv);
            socket->send("World"sv);
        }
    });
}

TEST_F(AsyncSocketTest, SendBufferMonotonicallyDecreases) {
    auto large_message = createLargeMessage("Hello World", 20);  // >10MB
    absl::Notification socketClosed;

    mServer = std::make_unique<SimpleTestServer>();
    mServer->start();
    auto socket = mServer->connect();
    socket->setOnCloseCallback([&socketClosed] { socketClosed.Notify(); });

    socket->send(large_message);
    socket->end();

    auto lastSendBufferSize = StorageCapacity(socket->raw()->sendBuffer());
    VLOG(1) << "Initial send buffer size: " << lastSendBufferSize;

    while (socket->raw()->sendBuffer() > 0) {
        auto currentSendBufferSize = StorageCapacity(socket->raw()->sendBuffer());
        EXPECT_LE(currentSendBufferSize.bytes(), lastSendBufferSize.bytes());
        lastSendBufferSize = currentSendBufferSize;
        // Yield to allow the socket to process data.
        std::this_thread::yield();
    }

    socket->end();
    EXPECT_TRUE(socketClosed.WaitForNotificationWithTimeout(absl::Seconds(5)));
}

TEST_F(AsyncSocketTest, SendLargeBlobs) {
    auto message = createLargeMessage("Hello World", 20);  // >10MB

    // The whole message should arrive at the server.
    runTest([&message](auto socket) {
        for (int i = 0; i < 10; i++) {
            socket->send(message);
        }
    });
}

TEST_F(AsyncSocketTest, IsThreadSafe) {
    // Many threads can send data over the same socket and it should not
    // become a mess.
    runTest([](auto socket) {
        std::string_view message = "Hello World!";
        std::vector<std::thread> senders;
        senders.reserve(100);

        for (int i = 0; i < 100; i++) {
            senders.emplace_back([socket, message] { socket->send(message); });
        }

        for (auto& sender : senders) {
            sender.join();
        }
    });
}

TEST_F(AsyncSocketTest, ReceivesAnEcho) {
    // Define a server that echoes back whatever it receives.
    auto echoHandler = [](auto msg) { return std::string(msg); };
    auto largeMessage = createLargeMessage("Hello World", 11);  // >16k

    runTest([&](auto socket) { socket->send(largeMessage); }, echoHandler);
}

TEST_F(AsyncSocketTest, ReceivesAClose) {
    absl::Notification socketClosed;

    mServer = std::make_unique<SimpleTestServer>();
    mServer->start();
    auto socket = mServer->connect();
    socket->setOnCloseCallback([&socketClosed] { socketClosed.Notify(); });
    socket->end();

    // Wait for the notification with a timeout to be safe.
    EXPECT_TRUE(socketClosed.WaitForNotificationWithTimeout(absl::Seconds(1)));
}

}  // namespace android::base::testing