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
#include "android/fingerprint/FingerprintDevice.h"

#include <android/base/testing/TestSystem.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/goldfish/config/fake-avd.h"
#include "goldfish//async/testing/test_event_loop.h"
#include "goldfish/devices/test_connector_registry.h"
#include "goldfish/devices/test_socket.h"

namespace goldfish::devices::fingerprint {

using android::base::TestSystem;
using async::testing::TestEventLoop;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;

class FingerprintDeviceTest : public ::testing::Test {
    void SetUp() override {
        mClientLoop = TestEventLoop::create();
        mQemuLoop = TestEventLoop::create();

        IFingerprintDevice::registerDevice(&registry, mClientLoop.get(), mQemuLoop.get());

        device = registry.constructHalDevice<IFingerprintDevice>();
        test_socket = registry.halSocket();
        clear();
        device->onConnect();
    }

  public:
    void clear() { test_socket->storage.clear(); }

  protected:
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;

    TestConnectorRegistry registry;
    TestHalSocket* test_socket;
    IFingerprintDevice* device;
};

TEST_F(FingerprintDeviceTest, canCreateDevice) {
    EXPECT_NE(device, nullptr);
}

TEST_F(FingerprintDeviceTest, canTouch) {
    device->touch(1);
    EXPECT_THAT(test_socket->storage, Eq("0004on:1"));
}

TEST_F(FingerprintDeviceTest, canRelease) {
    device->release();
    EXPECT_THAT(test_socket->storage, Eq("0003off"));
}

}  // namespace goldfish::devices::fingerprint