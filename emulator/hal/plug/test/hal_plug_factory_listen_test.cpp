// Copyright 2025 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fake_vsock.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/hal/plug/HalPlugFactory.h"

namespace goldfish {
namespace devices {
namespace {

using ::goldfish::async::testing::TestEventLoop;

class HalPlugFactoryListenTest : public ::testing::Test {
  public:
    HalPlugFactoryListenTest() = default;

    void SetUp() override {
        mClientLoop = async::testing::TestEventLoop::create();
        mQemuLoop = async::testing::TestEventLoop::create();
        // Set up any necessary objects for the tests.
    }

  protected:
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;
};

class MockSocket : public cable::ISocket {
  public:
    ~MockSocket() { VLOG(1) << "Destroying our mock socket"; }
    MOCK_METHOD(void, sendAsync, (const void* data, size_t size), (override));
    MOCK_METHOD(cable::PlugPtr, switchPlug, (cable::PlugPtr newPlug), (override));
    MOCK_METHOD(cable::PlugPtr, unplugImpl, (), (override));
};

// A simple HalPlug implementation for testing.
class TestHalPlug : public HalPlug {
  public:
    TestHalPlug() = default;
    ~TestHalPlug() override = default;

    void onConnect() override { mOnConnectCalled = true; }
    bool onConnectCalled() const { return mOnConnectCalled; }
    void onReceive(std::string_view data) override {}
    void onClose() override {}

  private:
    bool mOnConnectCalled = false;
};

TEST_F(HalPlugFactoryListenTest, ListenSuccess) {
    const int port = 1234;
    bool factoryCalled = false;
    std::shared_ptr<TestHalPlug> createdPlug;
    MockSocket* mockSocket = new MockSocket();

    auto factory = [&]() {
        factoryCalled = true;
        createdPlug = std::make_shared<TestHalPlug>();
        return createdPlug;
    };

    vsock::set_fake_listen_fn([&](uint32_t port, vsock::HostPortListener listener) {
        listener(SocketPtr(mockSocket));
        return true;
    });

    // Start listening.
    bool result = HalPlugFactory::listen(port, factory, mClientLoop.get(), mQemuLoop.get());
    EXPECT_TRUE(result);

    // The factory should have been called on the QEMU loop.
    mQemuLoop->runOne();
    EXPECT_TRUE(factoryCalled);
    ASSERT_NE(nullptr, createdPlug);

    // The onConnect method should be called on the client loop.
    mClientLoop->runOne();
    EXPECT_TRUE(createdPlug->onConnectCalled());

    // Cleanup..
    EXPECT_CALL(*mockSocket, unplugImpl()).Times(1);
    createdPlug.reset();
    delete mockSocket;
}

}  // namespace
}  // namespace devices
}  // namespace goldfish
