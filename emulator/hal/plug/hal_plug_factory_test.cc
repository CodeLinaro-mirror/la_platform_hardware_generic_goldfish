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
#include "goldfish/devices/hal_plug_factory.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "emulator/hal/plug/test/fake_vsock.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/internal/hal_plug.h"

namespace goldfish {
namespace devices {
namespace {

void onFlowControlEvent(bool /*enableReading*/) {}

using ::goldfish::async::testing::TestEventLoop;

// Mock for the real ISocket that lives on the QEMU thread.
class MockSocket : public cable::ISocket {
  public:
    ~MockSocket() { VLOG(1) << "Destroying our mock socket"; }
    MOCK_METHOD(void, SendAsync, (const void* data, size_t size), (override));
    MOCK_METHOD(cable::PlugPtr, SwitchPlug, (cable::PlugPtr newPlug), (override));
    MOCK_METHOD(cable::PlugPtr, UnplugImpl, (), (override));
};

class TestHalPlug : public HalPlug {
  public:
    TestHalPlug() = default;
    ~TestHalPlug() override = default;

    void OnConnect() override { mOnConnectCalled = true; }
    void OnReceive(std::string_view data) override { mReceivedData.emplace_back(data); }
    void OnClose() override { mOnCloseCalled = true; }

    bool onConnectCalled() const { return mOnConnectCalled; }
    bool onCloseCalled() const { return mOnCloseCalled; }
    const std::vector<std::string>& receivedData() const { return mReceivedData; }

    void close() { Socket()->Close(); }

  private:
    bool mOnConnectCalled = false;
    bool mOnCloseCalled = false;
    std::vector<std::string> mReceivedData;
};

class HalPlugFactoryTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mClientLoop = TestEventLoop::create();
        mQemuLoop = TestEventLoop::create();
        // Set up any necessary objects for the tests.
    }

    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;
};

TEST_F(HalPlugFactoryTest, WrapHalPlug) {
    bool factoryCalled = false;
    auto testPlug = std::make_shared<TestHalPlug>();
    auto factory = [&]() {
        factoryCalled = true;
        return testPlug;
    };

    auto mockSocketRaw = new MockSocket();
    cable::SocketPtr fakeSocket(mockSocketRaw);

    auto plug = HalPlugFactory::WrapHalPlug(std::move(fakeSocket), factory, mClientLoop.get(),
                                            mQemuLoop.get());

    EXPECT_TRUE(factoryCalled);
    EXPECT_NE(nullptr, plug);

    // onConnect should be posted to the client loop.
    EXPECT_FALSE(testPlug->onConnectCalled());
    mClientLoop->runOne();
    EXPECT_TRUE(testPlug->onConnectCalled());

    // Let's simulate the cleanup cycle
    EXPECT_CALL(*mockSocketRaw, UnplugImpl()).Times(1);
    EXPECT_EQ(plug.use_count(), 1);
    plug.reset();
    EXPECT_EQ(plug.use_count(), 0);
    EXPECT_EQ(testPlug.use_count(), 1);
    testPlug.reset();
    delete mockSocketRaw;
}

TEST_F(HalPlugFactoryTest, ConnectSuccess) {
    auto mockSocketRaw = new MockSocket();

    bool factoryCalled = false;
    auto testPlug = std::make_shared<TestHalPlug>();
    auto factory = [&]() {
        factoryCalled = true;
        return testPlug;
    };

    bool connectCalled = false;
    vsock::set_fake_connect_fn([&](uint32_t port, devices::cable::PlugPtr plug) {
        connectCalled = true;
        return cable::SocketPtr(mockSocketRaw);
    });

    auto plug = HalPlugFactory::Connect(5678, factory, mClientLoop.get(), mQemuLoop.get(),
                                        onFlowControlEvent);

    EXPECT_TRUE(factoryCalled);
    EXPECT_TRUE(connectCalled);
    EXPECT_NE(nullptr, plug);

    // Let's simulate the cleanup cycle
    EXPECT_CALL(*mockSocketRaw, UnplugImpl()).Times(1);
    EXPECT_EQ(plug.use_count(), 1);
    plug.reset();
    EXPECT_EQ(plug.use_count(), 0);
    EXPECT_EQ(testPlug.use_count(), 1);
    testPlug.reset();
    delete mockSocketRaw;
}

TEST_F(HalPlugFactoryTest, ConnectFailure) {
    bool factoryCalled = false;
    auto testPlug = std::make_shared<TestHalPlug>();
    auto factory = [&]() {
        factoryCalled = true;
        return testPlug;
    };

    bool connectCalled = false;
    vsock::set_fake_connect_fn([&](uint32_t port, devices::cable::PlugPtr plug) {
        connectCalled = true;
        return nullptr;
    });

    auto plug = HalPlugFactory::Connect(5678, factory, mClientLoop.get(), mQemuLoop.get(),
                                        onFlowControlEvent);

    EXPECT_TRUE(factoryCalled);
    EXPECT_TRUE(connectCalled);
    EXPECT_EQ(nullptr, plug);
}

}  // namespace
}  // namespace devices
}  // namespace goldfish