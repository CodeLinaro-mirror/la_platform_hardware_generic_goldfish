// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "goldfish/vsock/marshalling_plug.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "fake_qemu_callbacks.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/listen.h"

using namespace goldfish::async;
using namespace goldfish::devices::cable;
using namespace std::chrono_literals;

class MockSocket : public ISocket {
  public:
    MOCK_METHOD(void, sendAsync, (const void* data, size_t size), (override));
    MOCK_METHOD(PlugPtr, switchPlug, (PlugPtr newPlug), (override));
    MOCK_METHOD(void, setDataSniffer, (std::unique_ptr<IDataSniffer> sniffer), (override));
    MOCK_METHOD(PlugPtr, unplugImpl, (), (override));
};

class MockPlug : public IPlug {
  public:
    MOCK_METHOD(bool, onReceive, (const void* data, size_t size), (override));
    MOCK_METHOD(void, onConnect, (), (override));
    MOCK_METHOD(SocketPtr, onUnplug, (), (override));
    MOCK_METHOD(bool, supportsLoadingFromSnapshot, (), (const, override));
    MOCK_METHOD(TypeId, getSnapshotTypeId, (), (const, override));
    MOCK_METHOD(bool, saveStateToSnapshot, (goldfish::archive::IWriter & writer),
                (const, override));
};

class MockDataSniffer : public IDataSniffer {
  public:
    MOCK_METHOD(void, toSocket, (const void* data, size_t dataSize), (override));
    MOCK_METHOD(void, toPlug, (const void* data, size_t dataSize), (override));
};
class MockWriter : public goldfish::archive::IWriter {
  public:
    MOCK_METHOD(void, write, (const void* buffer, size_t size), (override));
};

namespace {
// To hold the plug passed to connect()
std::shared_ptr<goldfish::devices::cable::IPlug> g_last_connect_plug;
// To hold the listener passed to listen()
goldfish::vsock::HostPortListener g_last_listen_listener;
}  // namespace

// Fake implementations for testing
namespace goldfish::vsock {

devices::cable::SocketPtr connect(uint32_t port, devices::cable::PlugPtr plug) {
    g_last_connect_plug = plug;
    // Return a mock socket for testing.
    return SocketPtr(new testing::StrictMock<MockSocket>());
}

bool listen(uint32_t port, HostPortListener listener) {
    g_last_listen_listener = listener;
    return true;
}

}  // namespace goldfish::vsock

// =============================================================================
// Test Fixture: MarshallingTest
// =============================================================================
//
// This test fixture sets up a simulated environment with two distinct threads
// and event loops to test the thread-safe marshalling logic of MarshallingPlug
// and MarshallingSocket.
//
// THREADING MODEL:
//
// 1. The QEMU Thread (Main Test Thread):
//    - The main thread that executes the TEST_F test bodies acts as the
//      simulated "QEMU thread".
//    - It uses a fake QEMU event loop (`mQemuLoop`), which is manually
//      "pumped" or advanced by the `runUntil()` helper function. This function
//      repeatedly calls `fake_qemu_advance_ms(1)` to process events queued
//      on the fake QEMU loop.
//
// 2. The Client Thread (`mClientThread`):
//    - A separate background thread that runs a real `LibuvEventLoop`
//      (`mClientLoop`).
//    - This simulates an external client (e.g., a plugin) operating on its
//      own event loop, completely separate from the main QEMU loop.
//
// SYNCHRONIZATION:
//
// - The tests use `std::promise` and `std::future` to synchronize and wait for
//   asynchronous operations to complete between the two threads.
// - When a test needs to wait for a task posted to the QEMU thread, it calls
//   `runUntil(future)`, which drives the QEMU event loop until the promise is
//   fulfilled.
// - When a test needs to wait for a task on the client thread, it can simply
//   call `future.wait()` or `future.get()`, as the client loop runs actively
//   in the background.

class MarshallingTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Set up client loop (libuv) in a background thread.
        mClientLoop = std::make_unique<goldfish::async::LibuvEventLoop>();
        mClientThread = std::thread([this]() { mClientLoop->run(); });

        // Set up QEMU loop (fake).
        initializeQemuEventLoop();
        mQemuLoop = goldfish::async::getQemuEventLoop();
    }

    void TearDown() override {
        // Shutdown client loop.
        if (mClientThread.joinable()) {
            mClientLoop->shutdown(1s).wait();
            mClientLoop->stop();
            mClientThread.join();
        }
        mClientLoop.reset();
        mKeptAliveSocket.reset();

        // Reset QEMU loop.
        fake_qemu_reset();
        g_last_connect_plug.reset();
        g_last_listen_listener = nullptr;
    }

    template <typename T>
    void runUntil(std::future<T>& future) {
        while (future.wait_for(0ms) != std::future_status::ready) {
            fake_qemu_advance_ms(1);
        }
    }

    void runUntil(std::future<void>& future) {
        while (future.wait_for(0ms) != std::future_status::ready) {
            fake_qemu_advance_ms(1);
        }
    }

    std::unique_ptr<goldfish::async::EventLoop> mClientLoop;
    goldfish::async::EventLoop* mQemuLoop = nullptr;
    std::thread mClientThread;
    SocketPtr mKeptAliveSocket;
};

TEST_F(MarshallingTest, MarshallingSocketSendAsync) {
    auto rawSocketPtr = new testing::StrictMock<MockSocket>();
    SocketPtr rawSocket(rawSocketPtr);
    std::promise<void> done_promise;
    auto done_future = done_promise.get_future();

    EXPECT_CALL(*rawSocketPtr, sendAsync)
            .WillOnce([this, &done_promise](const void* data, size_t size) {
                EXPECT_TRUE(mQemuLoop->isOnLoopThread());
                EXPECT_EQ(std::string((const char*)data, size), "hello");
                done_promise.set_value();
            });
    EXPECT_CALL(*rawSocketPtr, unplugImpl()).WillOnce([rawSocketPtr]() {
        delete rawSocketPtr;
        return nullptr;
    });

    MarshallingSocket marshallingSocket(std::move(rawSocket), mClientLoop.get(), {});

    marshallingSocket.sendAsync("hello", 5);

    runUntil(done_future);
}

TEST_F(MarshallingTest, MarshallingSocketSwitchPlug) {
    auto rawSocketPtr = new testing::StrictMock<MockSocket>();
    SocketPtr rawSocket(rawSocketPtr);
    auto newPlug = std::make_shared<testing::StrictMock<MockPlug>>();

    EXPECT_CALL(*rawSocketPtr, switchPlug).WillOnce([this, &newPlug](PlugPtr p) {
        // Assert it's called on the correct thread.
        EXPECT_TRUE(mQemuLoop->isOnLoopThread());
        EXPECT_EQ(p, newPlug);
        return nullptr;
    });
    EXPECT_CALL(*rawSocketPtr, unplugImpl()).WillOnce([rawSocketPtr]() {
        delete rawSocketPtr;
        return nullptr;
    });

    MarshallingSocket marshallingSocket(std::move(rawSocket), mClientLoop.get(), {});

    // Call from the main test thread (simulating the QEMU thread)
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    marshallingSocket.switchPlug(newPlug);
}

TEST_F(MarshallingTest, MarshallingSocketSetDataSniffer) {
    auto rawSocketPtr = new testing::StrictMock<MockSocket>();
    SocketPtr rawSocket(rawSocketPtr);
    auto sniffer = std::make_unique<MockDataSniffer>();
    MockDataSniffer* sniffer_ptr = sniffer.get();
    std::promise<void> done_promise;
    auto done_future = done_promise.get_future();

    EXPECT_CALL(*rawSocketPtr, setDataSniffer)
            .WillOnce([this, &done_promise, sniffer_ptr](std::unique_ptr<IDataSniffer> s) {
                EXPECT_TRUE(mQemuLoop->isOnLoopThread());
                EXPECT_EQ(s.get(), sniffer_ptr);
                done_promise.set_value();
            });
    EXPECT_CALL(*rawSocketPtr, unplugImpl()).WillOnce([rawSocketPtr]() {
        delete rawSocketPtr;
        return nullptr;
    });

    MarshallingSocket marshallingSocket(std::move(rawSocket), mClientLoop.get(), {});
    marshallingSocket.setDataSniffer(std::move(sniffer));

    runUntil(done_future);
}

TEST_F(MarshallingTest, MarshallingSocketUnplugImplFromClientThread) {
    auto rawSocketPtr = new testing::StrictMock<MockSocket>();
    SocketPtr rawSocket(rawSocketPtr);
    auto clientPlug = std::make_shared<MockPlug>();
    std::promise<void> unplug_done_promise;
    auto unplug_done_future = unplug_done_promise.get_future();

    UnpluggerFn unplugger = [this, rawSocketPtr, &clientPlug](ISocket* s) {
        // EXPECT_TRUE(mQemuLoop->isOnLoopThread());
        EXPECT_EQ(s, rawSocketPtr);
        return clientPlug;
    };
    EXPECT_CALL(*rawSocketPtr, unplugImpl()).WillOnce([rawSocketPtr]() {
        delete rawSocketPtr;
        return nullptr;
    });

    MarshallingSocket marshallingSocket(std::move(rawSocket), mClientLoop.get(), unplugger);

    std::thread t([&]() {
        PlugPtr p = marshallingSocket.unplugImpl();
        EXPECT_EQ(p, clientPlug);
        unplug_done_promise.set_value();
    });

    runUntil(unplug_done_future);
    t.join();
}

TEST_F(MarshallingTest, MarshallingSocketUnplugImplFromQemuThread) {
    auto rawSocketPtr = new testing::StrictMock<MockSocket>();
    SocketPtr rawSocket(rawSocketPtr);
    auto clientPlug = std::make_shared<MockPlug>();

    UnpluggerFn unplugger = [this, rawSocketPtr, &clientPlug](ISocket* s) {
        EXPECT_TRUE(mQemuLoop->isOnLoopThread());
        EXPECT_EQ(s, rawSocketPtr);
        return clientPlug;
    };
    EXPECT_CALL(*rawSocketPtr, unplugImpl()).WillOnce([rawSocketPtr]() {
        delete rawSocketPtr;
        return nullptr;
    });

    MarshallingSocket marshallingSocket(std::move(rawSocket), mClientLoop.get(), unplugger);

    // We are on QEMU thread in the test.
    PlugPtr p = marshallingSocket.unplugImpl();
    EXPECT_EQ(p, clientPlug);
}

TEST_F(MarshallingTest, MarshallingPlugOnReceive) {
    auto clientPlug = std::make_shared<testing::StrictMock<MockPlug>>();
    std::promise<void> received_promise;
    auto received_future = received_promise.get_future();

    EXPECT_CALL(*clientPlug, onReceive)
            .WillOnce([this, &received_promise](const void* data, size_t size) {
                EXPECT_TRUE(mClientLoop->isOnLoopThread());
                EXPECT_FALSE(mQemuLoop->isOnLoopThread());
                EXPECT_EQ(std::string((const char*)data, size), "world");
                received_promise.set_value();
                return true;
            });

    MarshallingPlug marshallingPlug(mClientLoop.get(), clientPlug);

    // Simulate call from QEMU loop.
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    bool result = marshallingPlug.onReceive("world", 5);
    EXPECT_TRUE(result);  // Should return true immediately.

    // Wait for the posted task to execute on the client thread.
    received_future.wait_for(1s);
}

TEST_F(MarshallingTest, MarshallingPlugOnConnect) {
    auto clientPlug = std::make_shared<testing::StrictMock<MockPlug>>();

    EXPECT_CALL(*clientPlug, onConnect).WillOnce([this]() {
        // Does not get marshalled, so still on qemu thread.
        EXPECT_TRUE(mQemuLoop->isOnLoopThread());
    });

    MarshallingPlug marshallingPlug(mClientLoop.get(), clientPlug);

    // Simulate call from QEMU loop.
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    marshallingPlug.onConnect();
}

TEST_F(MarshallingTest, MarshallingPlugOnUnplug) {
    auto clientPlug = std::make_shared<testing::StrictMock<MockPlug>>();
    auto returnedSocket = std::make_unique<MockSocket>();
    ISocket* returnedSocketPtr = returnedSocket.get();

    EXPECT_CALL(*clientPlug, onUnplug)
            .WillOnce([returnedSocket = std::move(returnedSocket)]() mutable -> SocketPtr {
                return SocketPtr(returnedSocket.release());
            });

    MarshallingPlug marshallingPlug(mClientLoop.get(), clientPlug);

    // Simulate call from QEMU loop.
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    SocketPtr result = marshallingPlug.onUnplug();
    EXPECT_EQ(result.get(), returnedSocketPtr);
}

TEST_F(MarshallingTest, MarshallingPlugSupportsLoadingFromSnapshot) {
    auto clientPlug = std::make_shared<testing::StrictMock<MockPlug>>();

    EXPECT_CALL(*clientPlug, supportsLoadingFromSnapshot()).WillOnce([this]() {
        // EXPECT_TRUE(mClientLoop->isOnLoopThread());
        return true;
    });

    MarshallingPlug marshallingPlug(mClientLoop.get(), clientPlug);
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    EXPECT_TRUE(marshallingPlug.supportsLoadingFromSnapshot());
}

TEST_F(MarshallingTest, MarshallingPlugGetSnapshotTypeId) {
    auto clientPlug = std::make_shared<testing::StrictMock<MockPlug>>();
    const IPlug::TypeId testTypeId = "12345";

    EXPECT_CALL(*clientPlug, getSnapshotTypeId()).WillOnce([this, testTypeId]() {
        // EXPECT_TRUE(mClientLoop->isOnLoopThread());
        return testTypeId;
    });

    MarshallingPlug marshallingPlug(mClientLoop.get(), clientPlug);
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    EXPECT_EQ(marshallingPlug.getSnapshotTypeId(), testTypeId);
}

TEST_F(MarshallingTest, MarshallingPlugSaveStateToSnapshot) {
    auto clientPlug = std::make_shared<testing::StrictMock<MockPlug>>();
    MockWriter writer;

    EXPECT_CALL(*clientPlug, saveStateToSnapshot(testing::Ref(writer)))
            .WillOnce([this](auto& writer_arg) {
                // This does not get marshalled, so it's still on the qemu thread.
                EXPECT_TRUE(mQemuLoop->isOnLoopThread());
                return true;
            });

    MarshallingPlug marshallingPlug(mClientLoop.get(), clientPlug);
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    EXPECT_TRUE(marshallingPlug.saveStateToSnapshot(writer));
}

TEST_F(MarshallingTest, ConnectWithMarshalling) {
    auto clientPlug = std::make_shared<MockPlug>();
    std::promise<SocketPtr> socket_promise;
    auto socket_future = socket_promise.get_future();

    // Call from client thread.
    auto future_status = mClientLoop->post([&]() -> SocketPtr {
        EXPECT_TRUE(mClientLoop->isOnLoopThread());
        auto value = goldfish::devices::cable::connectWithMarshalling(
                1234, clientPlug, mClientLoop.get(), MarshallingSocket::unpluggerFor<MockSocket>());
        socket_promise.set_value(std::move(value));
        return nullptr;
    });
    ASSERT_TRUE(future_status.ok());

    runUntil(socket_future);
    auto socket = socket_future.get();

    // Check that the returned socket is a MarshallingSocket.
    ASSERT_NE(socket, nullptr);
    auto* marshallingSocket =
            dynamic_cast<goldfish::devices::cable::MarshallingSocket*>(socket.get());
    ASSERT_NE(marshallingSocket, nullptr);

    // Check that vsock::connect was called with a MarshallingPlug.
    ASSERT_NE(g_last_connect_plug, nullptr);
    auto* marshallingPlug =
            dynamic_cast<goldfish::devices::cable::MarshallingPlug*>(g_last_connect_plug.get());
    ASSERT_NE(marshallingPlug, nullptr);

    auto* mockRawSocket =
            dynamic_cast<testing::StrictMock<MockSocket>*>(marshallingSocket->mWrappedSocket.get());
    EXPECT_CALL(*mockRawSocket, unplugImpl()).WillOnce(testing::Return(nullptr));
    testing::Mock::AllowLeak(mockRawSocket);
}

TEST_F(MarshallingTest, ConnectDataFlow) {
    auto clientPlug = std::make_shared<testing::StrictMock<MockPlug>>();
    std::promise<void> received_promise;
    auto received_future = received_promise.get_future();

    // 1. Call connectWithMarshalling
    auto socket = goldfish::devices::cable::connectWithMarshalling(
            1234, clientPlug, mClientLoop.get(), MarshallingSocket::unpluggerFor<MockSocket>());

    // g_last_connect_plug is now the MarshallingPlug.
    ASSERT_NE(g_last_connect_plug, nullptr);

    // 2. Setup expectation on client plug
    EXPECT_CALL(*clientPlug, onReceive)
            .WillOnce([this, &received_promise](const void* data, size_t size) {
                EXPECT_TRUE(mClientLoop->isOnLoopThread());
                received_promise.set_value();
                return true;
            });

    // 3. Simulate QEMU socket receiving data. This calls
    // MarshallingPlug::onReceive. This needs to be on the QEMU thread. The test
    // main thread is the QEMU thread.
    g_last_connect_plug->onReceive("test", 4);

    // 4. Wait for marshalling to complete.
    received_future.wait();

    auto* marshallingSocket =
            dynamic_cast<goldfish::devices::cable::MarshallingSocket*>(socket.get());
    auto* mockRawSocket =
            dynamic_cast<testing::StrictMock<MockSocket>*>(marshallingSocket->mWrappedSocket.get());
    EXPECT_CALL(*mockRawSocket, unplugImpl()).WillOnce(testing::Return(nullptr));
    testing::Mock::AllowLeak(mockRawSocket);
}

TEST_F(MarshallingTest, ListenWithMarshalling) {
    auto clientPlug = std::make_shared<MockPlug>();
    std::promise<void> listener_called_promise;
    auto listener_called_future = listener_called_promise.get_future();

    goldfish::vsock::HostPortListener listener = [this, &listener_called_promise,
                                                  &clientPlug](SocketPtr s) -> PlugOrSocket {
        EXPECT_TRUE(mClientLoop->isOnLoopThread());
        auto* marshallingSocket = dynamic_cast<MarshallingSocket*>(s.get());
        EXPECT_NE(marshallingSocket, nullptr);
        listener_called_promise.set_value();
        mKeptAliveSocket = std::move(s);
        return clientPlug;
    };

    auto unplugger = MarshallingSocket::unpluggerFor<MockSocket>();

    // Call from the main test thread, which is the QEMU thread.
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    bool success = listenWithMarshalling(5555, listener, mClientLoop.get(), unplugger);
    EXPECT_TRUE(success);
    ASSERT_TRUE(g_last_listen_listener);

    // Simulate a new connection on the QEMU thread. This will block until the
    // client thread processes the listener.
    SocketPtr qemuSocket(new MockSocket());
    PlugOrSocket result = g_last_listen_listener(std::move(qemuSocket));

    // Wait for the listener to have been called on the client thread.
    listener_called_future.wait();

    // Check that the returned plug is a MarshallingPlug.
    ASSERT_TRUE(std::holds_alternative<PlugPtr>(result));
    PlugPtr resultPlug = std::get<PlugPtr>(result);
    auto* marshallingPlug = dynamic_cast<MarshallingPlug*>(resultPlug.get());
    ASSERT_NE(marshallingPlug, nullptr);
}

TEST_F(MarshallingTest, ListenWithMarshallingRejection) {
    std::promise<void> listener_called_promise;
    auto listener_called_future = listener_called_promise.get_future();

    goldfish::vsock::HostPortListener listener =
            [this, &listener_called_promise](SocketPtr s) -> PlugOrSocket {
        EXPECT_TRUE(mClientLoop->isOnLoopThread());
        listener_called_promise.set_value();
        // Reject by returning the socket
        return s;
    };

    auto unplugger = MarshallingSocket::unpluggerFor<MockSocket>();

    // Call from the main test thread, which is the QEMU thread.
    ASSERT_TRUE(mQemuLoop->isOnLoopThread());
    bool success = listenWithMarshalling(5555, listener, mClientLoop.get(), unplugger);
    EXPECT_TRUE(success);
    ASSERT_TRUE(g_last_listen_listener);

    // Simulate a new connection on the QEMU thread. This will block until the
    // client thread processes the listener.
    SocketPtr qemuSocket(new MockSocket());
    ISocket* qemuSocketPtr = qemuSocket.get();
    PlugOrSocket result = g_last_listen_listener(std::move(qemuSocket));

    // Wait for the listener to have been called on the client thread.
    listener_called_future.wait();

    // Check that the returned value is the marshalling socket.
    ASSERT_TRUE(std::holds_alternative<SocketPtr>(result));
    SocketPtr resultSocket = std::move(std::get<SocketPtr>(result));
    auto* marshallingSocket = dynamic_cast<MarshallingSocket*>(resultSocket.get());
    ASSERT_NE(marshallingSocket, nullptr);
    EXPECT_EQ(marshallingSocket->mWrappedSocket.get(), qemuSocketPtr);
}