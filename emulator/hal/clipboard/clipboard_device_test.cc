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
#include "goldfish/devices/clipboard/clipboard_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/system/test-headers/android/base/testing/TestSystem.h"
#include "emulator/hal/connector/test/goldfish/devices/test_connector_registry.h"
#include "goldfish/async/testing/test_event_loop.h"

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

        IClipboardDevice::registerDevice(&mClipboardChannel, &registry, mClientLoop.get(),
                                         mQemuLoop.get());
        device = registry.constructHalDevice<IClipboardDevice>();

        test_socket = registry.halSocket();
        clear();
    }

  public:
    void sendGuestToHost(std::string_view msg) {
        uint32_t size = msg.size();
        device->onReceive(std::string(reinterpret_cast<const char*>(&size), sizeof(size)));
        device->onReceive(std::string(msg));
    }
    void clear() { test_socket->storage.clear(); }

  protected:
    avd_universe::clipboard::ClipboardChannel mClipboardChannel;
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
    sendGuestToHost("guestToHost");
    EXPECT_THAT(mClipboardChannel.guestToHost.getValue().contents, "guestToHost");
}

TEST_F(ClipboardDeviceTest, canSendClipboardData) {
    mClipboardChannel.hostToGuest.setValue({.contents = "hostToGuest"});
    EXPECT_THAT(test_socket->storage, HasSubstr("hostToGuest"));
}

}  // namespace goldfish::devices::clipboard