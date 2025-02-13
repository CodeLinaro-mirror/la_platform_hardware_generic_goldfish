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
#include "android/physics/SensorDevice.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_format.h"

#include "android/base/testing/TestSystem.h"
#include "android/goldfish/config/avd-test.h"
#include "android/physics/Sensors.h"
#include "goldfish/devices/test_connector_registry.h"
#include "goldfish/devices/test_socket.h"
namespace goldfish::devices::sensor {

using android::base::TestSystem;
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

class SensorDeviceTest : public android::goldfish::AvdTest {
    void SetUp() override {
        ISensorDevice::registerDevice(&registry, avd(), registry.getLooper());
        device = registry.constructDevice<ISensorDevice>();
        test_socket = registry.getSocket();
        looper = registry.getLooper();
        clear();
    }

  public:
    void receive(std::string_view msg) { device->onReceive(msg.data(), msg.size()); }
    void clear() { test_socket->storage.clear(); }

    void setAcceleration(float x, float y, float z) {
        auto status = device->overrideSensor(AndroidSensor::ANDROID_SENSOR_ACCELERATION, {x, y, z});
        EXPECT_TRUE(status.ok());
    }

  protected:
    TestLooper* looper;
    TestConnectorRegistry registry;
    TestSocket* test_socket;
    ISensorDevice* device;
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
    looper->setVirtualTimeNs(1234567890);
    receive("set-delay:10");
    EXPECT_THAT(test_socket->storage, HasSubstr("0015guest-sync:123456789"));
}

TEST_F(SensorDeviceTest, setTimeOffset) {
    looper->setVirtualTimeNs(1234567890);
    receive("time:100");
    receive("set-delay:1");
    EXPECT_THAT(test_socket->storage, HasSubstr("000Eguest-sync:10"));
}

TEST_F(SensorDeviceTest, timeKeepsOnRolling) {
    looper->setVirtualTimeNs(1234567890);
    receive("set-delay:1");
    clear();
    EXPECT_THAT(test_socket->storage, Eq(""));
    looper->runWithTimeoutMs(50);

    // We should see a sync several times.
    EXPECT_THAT(countOccurrences(test_socket->storage, "guest-sync:"), Gt(10));
}

static const float TOLERANCE = 0.1f;
TEST_F(SensorDeviceTest, Portrait) {
    setAcceleration(0.0, 9.8, 0.0);  // Approximating gravity on Earth
    Rotation rotation = device->getDeviceRotation().value();
    EXPECT_FLOAT_EQ(rotation.xAxis, 0.0f);
    EXPECT_FLOAT_EQ(rotation.yAxis, 9.8f);
    EXPECT_FLOAT_EQ(rotation.zAxis, 0.0f);
    EXPECT_EQ(rotation.rotation, SkinRotation::PORTRAIT);
}

TEST_F(SensorDeviceTest, Landscape) {
    setAcceleration(9.8, 0.0, 0.0);
    Rotation rotation = device->getDeviceRotation().value();
    EXPECT_FLOAT_EQ(rotation.xAxis, 9.8f);
    EXPECT_FLOAT_EQ(rotation.yAxis, 0.0f);
    EXPECT_FLOAT_EQ(rotation.zAxis, 0.0f);
    EXPECT_EQ(rotation.rotation, SkinRotation::LANDSCAPE);
}

TEST_F(SensorDeviceTest, ReversePortrait) {
    setAcceleration(0.0, -9.8, 0.0);
    Rotation rotation = device->getDeviceRotation().value();
    EXPECT_FLOAT_EQ(rotation.xAxis, 0.0f);
    EXPECT_FLOAT_EQ(rotation.yAxis, -9.8f);
    EXPECT_FLOAT_EQ(rotation.zAxis, 0.0f);
    EXPECT_EQ(rotation.rotation, SkinRotation::REVERSE_PORTRAIT);
}

TEST_F(SensorDeviceTest, ReverseLandscape) {
    setAcceleration(-9.8, 0.0, 0.0);
    Rotation rotation = device->getDeviceRotation().value();
    EXPECT_FLOAT_EQ(rotation.xAxis, -9.8f);
    EXPECT_FLOAT_EQ(rotation.yAxis, 0.0f);
    EXPECT_FLOAT_EQ(rotation.zAxis, 0.0f);
    EXPECT_EQ(rotation.rotation, SkinRotation::REVERSE_LANDSCAPE);
}

TEST_F(SensorDeviceTest, Diagonal) {
    // Test a diagonal rotation to ensure proper normalization and dot product calculation
    setAcceleration(4.0, 4.0, 4.0);  // Not normalized
    Rotation rotation = device->getDeviceRotation().value();
    EXPECT_NEAR(rotation.xAxis, 4.0f, TOLERANCE);  // Expect the original values
    EXPECT_NEAR(rotation.yAxis, 4.0f, TOLERANCE);
    EXPECT_NEAR(rotation.zAxis, 4.0f, TOLERANCE);

    // Should pick something,
    bool rotationValid = false;
    auto calculatedRotation = rotation.rotation;

    for (const auto& v : {SkinRotation::PORTRAIT, SkinRotation::LANDSCAPE,
                          SkinRotation::REVERSE_PORTRAIT, SkinRotation::REVERSE_LANDSCAPE}) {
        if (v == calculatedRotation) {
            rotationValid = true;
            break;
        }
    }

    EXPECT_TRUE(rotationValid);
}

TEST_F(SensorDeviceTest, ZeroGravity) {
    setAcceleration(0.0, 0.0, 0.0);
    Rotation rotation = device->getDeviceRotation().value();

    EXPECT_FLOAT_EQ(rotation.xAxis, 0.0f);
    EXPECT_FLOAT_EQ(rotation.yAxis, 0.0f);
    EXPECT_FLOAT_EQ(rotation.zAxis, 0.0f);

    // In a zero-g situation, the rotation can't be reliably determined.
    EXPECT_EQ(rotation.rotation, SkinRotation::PORTRAIT);  //
}

}  // namespace goldfish::devices::sensor