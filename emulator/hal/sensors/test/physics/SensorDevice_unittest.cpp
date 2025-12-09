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

#include "goldfish/devices/sensor/SensorDevice.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/strings/str_format.h"

#include "android/base/system/TestClock.h"
#include "android/base/testing/TestSystem.h"
#include "android/goldfish/config/fake-avd.h"
#include "goldfish//async/testing/test_event_loop.h"
#include "goldfish/devices/qemud.h"
#include "goldfish/devices/test_connector_registry.h"

namespace goldfish::devices::sensor {

using android::base::TestSystem;
using async::testing::TestEventLoop;
using ::goldfish::physics::Rotation;
using ::goldfish::physics::SkinRotation;
using ::goldfish::sensors::AndroidSensor;
using ::testing::_;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;

int countOccurrences(const std::string& text, const std::string& target) {
    int count = 0;
    std::string::size_type pos = 0;

    while ((pos = text.find(target, pos)) != std::string::npos) {
        ++count;
        pos += target.length();
    }

    return count;
}

class SensorDeviceTest : public ::testing::Test {
    void SetUp() override {
        mPhysicalModel = std::make_unique<PhysicalModel>(mAvd.hw());
        mClientLoop = TestEventLoop::create();
        mQemuLoop = TestEventLoop::create();

        ISensorDevice::registerDevice(mPhysicalModel.get(), &registry,
                                      /*avd_type=*/android::goldfish::DeviceType::kPhone,
                                      /*avd_api=*/30, mAvd.hw(), mClientLoop.get(), mQemuLoop.get(),
                                      &mClock);
        device = registry.constructHalDevice<ISensorDevice>();
        test_socket = registry.halSocket();
        clear();
        device->onConnect();
    }

  public:
    void receive(std::string_view msg) {
        (void)mClientLoop->post([&, this] { device->onReceive(qemud::encodeQemudPacket(msg)); });
        mClientLoop->runAll();
    }
    void clear() { test_socket->storage.clear(); }

  protected:
    android::goldfish::FakeAvd mAvd;
    std::unique_ptr<PhysicalModel> mPhysicalModel;
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;
    TestConnectorRegistry registry;
    android::base::TestClock mClock;
    ISensorDevice* device;
    TestHalSocket* test_socket;
};

TEST_F(SensorDeviceTest, canCreateDevice) {
    EXPECT_NE(device, nullptr);
}

TEST_F(SensorDeviceTest, canListSensors) {
    receive("list-sensors");
    EXPECT_THAT(test_socket->storage, Eq("0006133119"));
}

TEST_F(SensorDeviceTest, canSetSensors) {
    receive("set:acceleration:0");
    receive("set:gyroscope:0");
    clear();

    // The active set should have changed.
    receive("list-sensors");
    EXPECT_THAT(test_socket->storage, Eq("0006133116"));
}

TEST_F(SensorDeviceTest, setDelayCausesATick) {
    mClock.set_time(absl::FromUnixNanos(1234567890));
    receive("set-delay:10");
    // The looper keeps ticking so just check for the first few digits.
    EXPECT_THAT(test_socket->storage, HasSubstr("0015guest-sync:1234"));
}

TEST_F(SensorDeviceTest, setTimeOffset) {
    mClock.set_time(absl::FromUnixNanos(1234567890));
    receive("time:100");
    receive("set-delay:1");
    EXPECT_THAT(test_socket->storage, HasSubstr("000Eguest-sync:10"));
}

TEST_F(SensorDeviceTest, timeKeepsOnRolling) {
    mClock.set_time(absl::FromUnixNanos(1234567890));
    receive("set-delay:1");
    clear();
    EXPECT_THAT(test_socket->storage, Eq(""));
    for (int i = 0; i < 11; i++) mClientLoop->advanceClock(std::chrono::milliseconds(10));

    // We should see a sync several times.
    EXPECT_THAT(countOccurrences(test_socket->storage, "guest-sync:"), Gt(10));
}

}  // namespace goldfish::devices::sensor
