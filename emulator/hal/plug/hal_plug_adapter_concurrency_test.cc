/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <deque>

#include "emulator/hal/plug/test/hal_plug_testing_friend.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/connector_registry_impl.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"
#include "goldfish/devices/marshalling_hal_socket.h"

using namespace goldfish::devices;
using namespace goldfish::async;
using namespace std::string_view_literals;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrEq;

using cable::IPlug;
using cable::ISocket;
using cable::PlugPtr;
using cable::SocketPtr;

using goldfish::async::testing::TestEventLoop;

// Mock for the real ISocket that lives on the QEMU thread.
class MockSocket : public cable::ISocket {
  public:
    MOCK_METHOD(void, sendAsync, (const void* data, size_t size), (override));
    MOCK_METHOD(cable::PlugPtr, switchPlug, (cable::PlugPtr newPlug), (override));
    MOCK_METHOD(cable::PlugPtr, unplugImpl, (), (override));
};

// Mock for the real HalPlug that lives on the client thread.
class MockHalPlug : public HalPlug {
  public:
    MOCK_METHOD(void, onConnect, (), (override));
    MOCK_METHOD(void, onReceive, (std::string_view data), (override));
    MOCK_METHOD(void, onClose, (), (override));

    std::shared_ptr<HalSocket> getSocket() { return socket(); }
};

TEST(HalPlugAdapterDeadlockTest, HostInitiatedCloseDuringCallbackDeadlocks) {
    // SCENARIO: Test for the classic deadlock condition.
    // This test orchestrates a sequence where:
    //  1. The QEMU thread notifies the client of an `onReceive` event.
    //  2. The client, from within its `onReceive` handler, decides to close
    //     the connection.
    //  3. The `close()` call uses a blocking `postAndWait` to wait for the
    //     QEMU thread.
    // This creates a deadlock: the client thread is waiting for the QEMU thread,
    // which is waiting for the client thread's `onReceive` handler to finish.
    // Our `ManualEventLoop` allows us to observe this state without hanging.
    //
    // NOTE: This test is expected to pass only *after* the deadlock is fixed
    // by making the `close()` call asynchronous. It serves as a regression test.
    // 1. Setup: Create manual loops and all the mock/adapter components.
    auto qemuLoop = TestEventLoop::create();
    auto clientLoop = TestEventLoop::create();

    auto mockHalPlug = std::make_shared<MockHalPlug>();
    auto mockSocketRaw = new MockSocket();
    cable::SocketPtr mockSocketPtr(mockSocketRaw);

    auto adapter = std::make_shared<HalPlugToIPlugAdapter>(clientLoop.get(), mockHalPlug);
    auto marshallingSocket =
            std::make_shared<MarshallingHalSocket>(std::move(mockSocketPtr), qemuLoop.get());

    HalPlugTesting::establishConnection(mockHalPlug.get(), marshallingSocket);

    // 2. Orchestration: Simulate the exact sequence leading to deadlock.

    //    Step A: An `onReceive` event arrives from the guest. This is a task
    //    posted to the QEMU loop.
    (void)qemuLoop->Post([&] { adapter->onReceive("some data", 9); });

    //    Step B: The QEMU loop runs. The adapter receives the event and posts
    //    a corresponding task to the client loop.
    EXPECT_GT(qemuLoop->taskCount(), 0);
    ASSERT_EQ(clientLoop->taskCount(), 0);
    qemuLoop->runOne();
    ASSERT_EQ(qemuLoop->taskCount(), 0);
    ASSERT_GT(clientLoop->taskCount(), 0);

    //    Step C: The client HAL decides to close the connection from within its
    //    `onReceive` handler. We set up the mock to trigger this behavior.
    EXPECT_CALL(*mockHalPlug, onReceive(_)).WillOnce(Invoke([&](std::string_view) {
        // This is the critical part. The client calls close() from the
        // callback. This will call postAndWait() on the qemuLoop->
        mockHalPlug->getSocket()->close();
    }));

    //    Step D: The `close()` call will post a task to the qemuLoop-> We need
    //    to mock the final `unplugImpl` call to prevent real cleanup.
    EXPECT_CALL(*mockSocketRaw, unplugImpl()).WillOnce(Invoke([] { return nullptr; }));

    //    Step E: Run the client task. This will execute the mock `onReceive`,
    //    which calls `close()`, which calls `postAndWait()` on the qemuLoop->
    clientLoop->runOne();

    // 3. Verification: Check for the deadlock state.
    //    The `postAndWait` has posted a task to the qemuLoop->
    //    The client loop is "stuck" inside its `onReceive` handler, waiting for
    //    the qemuLoop to finish, but it can't, because we control it.
    //    This demonstrates the deadlock: the client is waiting for QEMU, and
    //    QEMU is waiting for the client.
    EXPECT_GT(qemuLoop->taskCount(), 0)
            << "The close() call failed to post a task to the QEMU loop.";
    ASSERT_EQ(clientLoop->taskCount(), 0) << "The client loop should be empty and 'blocked'.";

    // In a real-world scenario, the test would hang here. Our manual control
    // allows us to inspect the state and prove the deadlock condition exists.
    // To clean up, we manually run the final QEMU task.
    qemuLoop->runOne();
    ASSERT_EQ(qemuLoop->taskCount(), 0);

    delete mockSocketRaw;
}

TEST(HalPlugAdapterConcurrencyTest, GuestOnUnplugRacesWithHostClose) {
    // SCENARIO: Test for a race condition between a host-initiated close and a
    // guest-initiated unplug.
    // This test orchestrates a sequence where:
    //  1. The client/host calls `socket()->close()`, which posts an async task
    //     to the QEMU loop.
    //  2. Before that task can run, a guest disconnect event arrives, and the
    //     QEMU thread runs `onUnplug()`.
    //  3. The test verifies that the teardown logic is executed exactly once,
    //     preventing a double-unplug or use-after-free.
    // 1. Setup
    auto qemuLoop = TestEventLoop::create();
    auto clientLoop = TestEventLoop::create();

    auto mockHalPlug = std::make_shared<MockHalPlug>();
    auto mockSocketRaw = new MockSocket();
    cable::SocketPtr mockSocketPtr(mockSocketRaw);
    auto adapter = std::make_shared<HalPlugToIPlugAdapter>(clientLoop.get(), mockHalPlug);
    auto marshallingSocket =
            std::make_shared<MarshallingHalSocket>(std::move(mockSocketPtr), qemuLoop.get());
    HalPlugTesting::establishConnection(mockHalPlug.get(), marshallingSocket);

    // 2. Orchestration
    //    Step A: Host initiates a close(). This posts a task to the QEMU loop.
    mockHalPlug->getSocket()->close();
    EXPECT_GT(qemuLoop->taskCount(), 0);
    ;

    //    Step B: Before the QEMU loop runs the close() task, a guest-initiated
    //    onUnplug event arrives and is executed.
    EXPECT_CALL(*mockHalPlug, onClose()).Times(1);  // Should only be called once.
    EXPECT_CALL(*mockSocketRaw, unplugImpl()).WillOnce(Invoke([&] {
        // onUnplug should return a nullptr as the plug is gone.
        return nullptr;
    }));

    adapter->onUnplug();
    ASSERT_GT(clientLoop->taskCount(), 0);
    clientLoop->runOne();  // Run the onClose() task.

    //    Step C: Now, the original close() task from the host runs.
    //    Because the connection is already closing, it should be a no-op.
    //    gMock will fail the test if unplugImpl() is called a second time.
    qemuLoop->runOne();

    // 3. Verification
    //    The key verification is that unplugImpl was only called once.
    delete mockSocketRaw;
}

struct TestSocket : public cable::ISocket {
    void sendAsync(const void* data, size_t size) override {}

    PlugPtr switchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr unplugImpl() override { return std::move(plug); }

    bool send(const std::string_view data) { return plug->onReceive(data.data(), data.size()); }

    PlugPtr plug;
};

TEST(ConnectorRegistryConcurrencyTest, UnplugDuringSetupRaceIsHandledSafely) {
    // SCENARIO: This is the most critical race condition test. It validates
    // that our fix in the ConnectorRegistry prevents a dangling socket pointer
    // and a system crash.
    // The sequence is:
    //  1. A guest connects, and the ConnectorRegistry's factory runs on the
    //     QEMU thread.
    //  2. The factory creates all the components, calls establishConnection,
    //     and posts `onConnect` to the client thread.
    //  3. CRITICAL: Before the client thread can run `onConnect`, the guest
    //     disconnects, triggering `onUnplug` on the QEMU thread.
    //  4. We verify that the system handles this gracefully: `onConnect` and
    //     `onClose` are both delivered to the client, and the connection is
    //     cleanly torn down exactly once.

    // 1. Setup
    auto qemuLoop = TestEventLoop::create();
    auto clientLoop = TestEventLoop::create();
    ConnectorRegistry registry;

    bool factoryCalled = false;
    auto mockHalPlug = std::make_shared<MockHalPlug>();
    auto mockSocketRaw = new TestSocket();
    cable::SocketPtr mockSocketPtr(mockSocketRaw);
    std::shared_ptr<cable::IPlug> connector;
    std::shared_ptr<cable::IPlug> adapter;
    std::string_view createDeviceCmd = "pipe:TestHalDevice:args\0"sv;

    // 2. Orchestration
    //    Step A: Register a HAL device.
    registry.registerHalDevice("TestHalDevice", clientLoop.get(), qemuLoop.get(),
                               [&](std::string_view /*args*/) {
                                   factoryCalled = true;
                                   return mockHalPlug;
                               });

    //    Step B: Use a manual listen function to simulate a connection and
    //    capture the adapter created by the registry.
    bool listenFnCalled = false;
    registry.listen([&](HostPortListener listener) {
        listenFnCalled = true;
        connector = std::get<cable::PlugPtr>(listener(cable::SocketPtr(mockSocketRaw)));
        return true;
    });
    ASSERT_TRUE(listenFnCalled);

    //    Step C: Run the registry's factory task on the QEMU loop. This will
    //    create the HalPlug, MarshallingSocket, call establishConnection, and
    //    post the onConnect task to the client loop.
    //.   Note: The adapter will have been switched from a Connector to our Adapter.
    EXPECT_TRUE(connector->onReceive(createDeviceCmd.data(), createDeviceCmd.size()));

    //.   Our connector will forward our "empty" arguments to the adapter.
    EXPECT_CALL(*mockHalPlug, onReceive("")).Times(1);
    EXPECT_TRUE(factoryCalled);

    //.   Creation takes place on QEMU loop, so no tasks are created, if we would
    //.   this would simply deadlock.
    ASSERT_EQ(qemuLoop->taskCount(), 0);

    //.   We did post the pending onConnect.
    ASSERT_GT(clientLoop->taskCount(), 0);  // onConnect is now pending.

    //    Step D: THE RACE. Before the client runs onConnect, the guest
    //    disconnects. We manually trigger onUnplug.
    EXPECT_CALL(*mockHalPlug, onClose()).Times(1);
    mockSocketRaw->plug->onUnplug();

    // The onUnplug call should have posted an onClose task to the client
    // AND a close/unplug task to the qemuLoop, because the socket was valid.

    // Note we have 3 tasks!
    // 1. The extra parameters from our connector ("")
    // 2. The onConnect call, actually became alive
    // 3. The onClose call
    ASSERT_EQ(clientLoop->taskCount(), 3);
    ASSERT_EQ(qemuLoop->taskCount(), 1);

    //    Step E: Now, let the client loop run. It should process onConnect
    //    first, then onClose.
    EXPECT_CALL(*mockHalPlug, onConnect()).Times(1);
    clientLoop->runOne();  // parameter delivery
    clientLoop->runOne();  // onConnect runs
    clientLoop->runOne();  // onClose runs

    //    Step F: Let the qemu loop run to clean up the socket.
    qemuLoop->runOne();  // unplugImpl runs

    // 3. Verification
    ASSERT_EQ(clientLoop->taskCount(), 0);
    ASSERT_EQ(qemuLoop->taskCount(), 0);
    // gMock will verify that all expected calls happened exactly once.
    mockSocketPtr.release();
    delete mockSocketRaw;
}

TEST(HalPlugAdapterConcurrencyTest, InFlightDataIsDeliveredAfterHostClose) {
    // SCENARIO: Verify that an in-flight onReceive event is still delivered
    // to the client even if the client initiates a close() before the event
    // is processed.
    //  1. QEMU thread posts an onReceive task to the client.
    //  2. Client thread calls close() before processing the onReceive task.
    //  3. We verify that onReceive is still called, followed by onClose.

    // 1. Setup
    auto qemuLoop = TestEventLoop::create();
    auto clientLoop = TestEventLoop::create();

    auto mockHalPlug = std::make_shared<MockHalPlug>();
    auto mockSocketRaw = new MockSocket();
    cable::SocketPtr mockSocketPtr(mockSocketRaw);
    auto adapter = std::make_shared<HalPlugToIPlugAdapter>(clientLoop.get(), mockHalPlug);
    auto marshallingSocket =
            std::make_shared<MarshallingHalSocket>(std::move(mockSocketPtr), qemuLoop.get());
    HalPlugTesting::establishConnection(mockHalPlug.get(), marshallingSocket);

    // 2. Orchestration
    //    Step A: QEMU thread sends data, posting an onReceive task to the client.
    adapter->onReceive("in-flight data", 14);
    ASSERT_GT(clientLoop->taskCount(), 0);

    //    Step B: Client thread initiates a close before processing the data.
    //    This posts the unplugImpl task to the QEMU loop.
    mockHalPlug->getSocket()->close();
    EXPECT_GT(qemuLoop->taskCount(), 0);
    ;

    //    Step C: Client loop runs. It must process the in-flight onReceive
    //    task first.
    EXPECT_CALL(*mockHalPlug, onReceive(StrEq("in-flight data"))).Times(1);
    clientLoop->runOne();

    //    Step D: QEMU loop runs the unplug task. This is the end of the line
    //    for a host-initiated close. No onClose event is expected.
    EXPECT_CALL(*mockSocketRaw, unplugImpl()).WillOnce(Invoke([] { return nullptr; }));
    qemuLoop->runOne();

    // 3. Verification
    ASSERT_EQ(clientLoop->taskCount(), 0);
    ASSERT_EQ(qemuLoop->taskCount(), 0);
    delete mockSocketRaw;
}
