// Copyright (C) 2017 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "android/clipboard/ClipboardDevice.h"

#include <android/base/testing/TestSystem.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/goldfish/config/fake-avd.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/test_connector_registry.h"
#include "goldfish/devices/test_socket.h"

namespace goldfish::devices::clipboard {

using android::base::TestSystem;
using goldfish::async::testing::TestEventLoop;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;

class ClipboardDeviceTest : public ::testing::Test {
    void SetUp() override {
        mClientLoop = TestEventLoop::create();
        mQemuLoop = TestEventLoop::create();

        android::goldfish::FakeAvd avd;
        IClipboardDevice::registerDevice(&registry, &avd, mClientLoop.get(), mQemuLoop.get());
        device = registry.constructHalDevice<IClipboardDevice>();

        test_socket = registry.halSocket();
        clear();
    }

  public:
    void receive(std::string_view msg) {
        uint32_t size = msg.size();
        device->onReceive(std::string(reinterpret_cast<const char*>(&size), sizeof(size)));
        device->onReceive(std::string(msg));
    }
    void clear() { test_socket->storage.clear(); }

  protected:
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;
    TestConnectorRegistry registry;
    TestHalSocket* test_socket;
    IClipboardDevice* device;
};

TEST_F(ClipboardDeviceTest, canCreateDevice) {
    EXPECT_NE(device, nullptr);
}

TEST_F(ClipboardDeviceTest, receiveClipboardDataFiresAnEvent) {
    std::string received;
    auto scoped = android::base::eventing::makeScopedCallback(
            *device, [&received](ClipboardData data) { received = data; });
    receive("hello");
    EXPECT_THAT(received, Eq("hello"));
}

TEST_F(ClipboardDeviceTest, canReceiveClipboardData) {
    receive("hello");
    EXPECT_THAT(device->getContents(), Eq("hello"));
}

TEST_F(ClipboardDeviceTest, canSendClipboardData) {
    device->setContents("hello");

    uint32_t size = absl::little_endian::Load32(test_socket->storage.data());
    EXPECT_THAT(size, Eq(5));
    EXPECT_THAT(test_socket->storage, HasSubstr("hello"));
}

}  // namespace goldfish::devices::clipboard