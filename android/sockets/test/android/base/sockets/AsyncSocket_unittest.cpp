// Copyright 2016 The Android Open Source Project
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

#include <chrono>
#include <thread>
#include <vector>

#include "android/base/sockets/SimpleTestServer.h"
#include "android/base/system/storage_capacity.h"

namespace android::base::testing {

using android::base::StorageCapacity;
using namespace std::chrono_literals;

class AsyncSocketTest : public ::testing::Test {
  protected:
    void SetUp() override {}

    void TearDown() override { mServer.waitUntilCompleted(); }
    SimpleTestServer mServer;
};

TEST(AsyncSocketTest, can_send_message) {
    SimpleTestServer mServer;
    std::string_view message("Hello World");
    mServer.start();
    auto socket = mServer.connect(ChecksumSocket::Mode::StoreAll);
    socket->send(message);
    socket->end();
    mServer.waitUntilCompleted();

    EXPECT_TRUE(socket->verifySend())
            << std::get<0>(socket->sendStrings()) << " != " << std::get<1>(socket->sendStrings());
    EXPECT_TRUE(socket->verifyRecv());
}

TEST(AsyncSocketTest, message_arrive_in_order) {
    SimpleTestServer mServer;
    std::string_view message1("Hello");
    std::string_view message2("World");
    mServer.start();
    auto socket = mServer.connect();
    for (int i = 0; i < 1024; i++) {
        socket->send(message1);
        socket->send(message2);
    }
    socket->end();
    mServer.waitUntilCompleted();

    EXPECT_TRUE(socket->verifySend());
    EXPECT_TRUE(socket->verifyRecv());
}

std::string largeMessage(const std::string& start, int exp) {
    // Let's expononentially grow our message.
    std::string message = start;
    for (int i = 0; i < exp; i++) {
        message += message;
    }
    return message;
}

TEST(AsyncSocketTest, send_buffer_monotonically_decreases) {
    SimpleTestServer mServer;
    // >10mb
    auto large_message = largeMessage("Hello World", 20);
    mServer.start();
    auto socket = mServer.connect();
    socket->send(large_message);
    auto sendBuffer = StorageCapacity(socket->raw()->sendBuffer());
    VLOG(1) << "Send buffer contains " << sendBuffer;

    while (socket->raw()->sendBuffer() > 0) {
        EXPECT_LE(StorageCapacity(socket->raw()->sendBuffer()), sendBuffer);
        sendBuffer = socket->raw()->sendBuffer();
    }

    socket->end();
    mServer.waitUntilCompleted();
}

TEST(AsyncSocketTest, send_large_blobs) {
    SimpleTestServer mServer;
    // >10mb
    auto message = largeMessage("Hello World", 20);

    mServer.start();
    auto socket = mServer.connect();
    for (int i = 0; i < 10; i++) {
        socket->send(message);
        auto sendBuffer = StorageCapacity(socket->raw()->sendBuffer());
        VLOG(1) << "Send buffer contains " << sendBuffer;
    }
    socket->end();
    mServer.waitUntilCompleted();

    EXPECT_TRUE(socket->verifySend());
    EXPECT_TRUE(socket->verifyRecv());
}

TEST(AsyncSocketTest, thread_safe) {
    SimpleTestServer mServer;
    // >10mb
    std::string_view message = "Hello World!";

    mServer.start();
    auto socket = mServer.connect();
    std::vector<std::thread> senders;
    for (int i = 0; i < 100; i++) {
        senders.emplace_back([socket, message] { socket->send(message); });
    }
    for (auto& sender : senders) {
        sender.join();
    }

    socket->end();
    mServer.waitUntilCompleted();

    auto [client_recv, server_send] = socket->recvStrings();
    auto [client_send, server_recv] = socket->sendStrings();
    EXPECT_EQ(client_send, server_recv) << "The server didn't receive the client message";
    EXPECT_EQ(client_recv, server_send) << "The client didn't receive what the server sent";
}

TEST(AsyncSocketTest, receives_an_echo) {
    SimpleTestServer mServer(0, [](auto msg) { return std::string(msg); });
    std::string_view message = "Hello World!";

    mServer.start();
    auto socket = mServer.connect(ChecksumSocket::Mode::StoreAll);
    socket->send(message);
    socket->end();
    // Give the server a chance to respond..
    std::this_thread::sleep_for(10ms);
    mServer.waitUntilCompleted();

    auto [client_recv, server_send] = socket->recvStrings();
    auto [client_send, server_recv] = socket->sendStrings();
    EXPECT_EQ(client_send, server_recv) << "The server didn't receive the client message";
    EXPECT_EQ(client_recv, server_send) << "The client didn't receive what the server sent";
}

TEST(AsyncSocketTest, receives_a_close) {
    SimpleTestServer mServer(0, [](auto msg) { return std::string(msg); });
    std::string_view message = "Hello World!";
    bool closeWasCalled = false;

    mServer.start();
    auto socket = mServer.connect(ChecksumSocket::Mode::StoreAll);
    socket->setOnCloseCallback([&closeWasCalled] { closeWasCalled = true; });
    socket->end();
    mServer.waitUntilCompleted();

    // Give the close event a chance to propagate.
    std::this_thread::sleep_for(10ms);
    EXPECT_TRUE(closeWasCalled);
}

}  // namespace android::base::testing
