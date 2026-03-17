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
#include "goldfish/devices/fingerprint/fingerprint_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/base/testing/TestSystem.h"
#include "goldfish/devices/test_connector_registry.h"
#include "goldfish//async/testing/test_event_loop.h"

namespace goldfish::devices::fingerprint {

using android::base::TestSystem;
using async::testing::TestEventLoop;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;

class FingerprintDeviceTest : public ::testing::Test {
    void SetUp() override {
        mClientLoop = TestEventLoop::Create();
        mQemuLoop = TestEventLoop::Create();

        IFingerprintDevice::RegisterDevice(&mTouchSensor, &registry, mClientLoop.get(),
                                           mQemuLoop.get());

        device = registry.ConstructHalDevice<IFingerprintDevice>();
        test_socket = registry.HalSocket();
        clear();
        device->OnConnect();
    }

  public:
    void clear() { test_socket->storage.clear(); }

  protected:
    ObservableFingerprintSensor mTouchSensor;
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
    mTouchSensor.SetValue(42);
    EXPECT_THAT(test_socket->storage, Eq("0005on:42"));
}

TEST_F(FingerprintDeviceTest, canRelease) {
    mTouchSensor.SetValue(avd_universe::fingerprint::kReleaseEvent);
    EXPECT_THAT(test_socket->storage, Eq("0003off"));
}

}  // namespace goldfish::devices::fingerprint
