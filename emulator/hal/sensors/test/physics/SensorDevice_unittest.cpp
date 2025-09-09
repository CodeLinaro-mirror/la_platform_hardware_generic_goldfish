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
#include "goldfish/devices/test_socket.h"

namespace goldfish::devices::sensor {

using android::base::TestSystem;
using async::testing::TestEventLoop;
using goldfish::physics::SkinRotation;
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
        mClientLoop = TestEventLoop::create();
        mQemuLoop = TestEventLoop::create();

        ISensorDevice::registerDevice(&registry, mAvd, mClientLoop.get(), mQemuLoop.get(), &mClock);
        device = registry.constructHalDevice<ISensorDevice>();
        test_socket = registry.halSocket();
        clear();
        device->onConnect();
    }

  public:
    void receive(std::string_view msg) { device->onReceive(qemud::encodeQemudPacket(msg)); }
    void clear() { test_socket->storage.clear(); }

    void setAcceleration(float x, float y, float z) {
        auto status = device->overrideSensor(AndroidSensor::ACCELERATION, {x, y, z});
        EXPECT_TRUE(status.ok());
    }

    void setProximity(float value) {
        auto status = device->overrideSensor(AndroidSensor::PROXIMITY, {value});
        EXPECT_TRUE(status.ok());
    }

  protected:
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;

    TestConnectorRegistry registry;
    ISensorDevice* device;
    TestHalSocket* test_socket;
    android::goldfish::FakeAvd mAvd;
    android::base::TestClock mClock;
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

// Mock callback for testing SensorObserver
class MockSensorCallback {
  public:
    MOCK_METHOD(void, onSensorChanged, (const SensorData& data));
};

TEST_F(SensorDeviceTest, SensorObserverNotNotifiedOnSameData) {
    // Create a SensorObserver for the accelerometer
    SensorObserver observer(&registry, AndroidSensor::ACCELERATION);

    // Create a mock callback
    MockSensorCallback mockCallback;

    // Set up expectations: onSensorChanged should be called once with the new data
    EXPECT_CALL(mockCallback, onSensorChanged(_)).Times(1);

    // Register the mock callback with the observer
    observer.addCallback(
            [&mockCallback](const SensorData& data) { mockCallback.onSensorChanged(data); });

    // Change the sensor data
    setAcceleration(1.0f, 2.0f, 3.0f);

    // No change, so no event
    setAcceleration(1.0f, 2.0f, 3.0f);
}

TEST_F(SensorDeviceTest, SensorObserverMultipleCallbacks) {
    // Create a SensorObserver for the accelerometer
    SensorObserver observer(&registry, AndroidSensor::ACCELERATION);

    // Create mock callbacks
    MockSensorCallback mockCallback1;
    MockSensorCallback mockCallback2;

    // Set up expectations: both callbacks should be called
    EXPECT_CALL(mockCallback1, onSensorChanged(Eq(SensorData{1.0f, 2.0f, 3.0f}))).Times(1);
    EXPECT_CALL(mockCallback2, onSensorChanged(Eq(SensorData{1.0f, 2.0f, 3.0f}))).Times(1);

    // Register the mock callbacks with the observer
    observer.addCallback(
            [&mockCallback1](const SensorData& data) { mockCallback1.onSensorChanged(data); });
    observer.addCallback(
            [&mockCallback2](const SensorData& data) { mockCallback2.onSensorChanged(data); });

    // Change the sensor data
    setAcceleration(1.0f, 2.0f, 3.0f);
}

TEST_F(SensorDeviceTest, SensorObserverDifferentSensors) {
    // Create a SensorObserver for the accelerometer and proximity
    // std::shared_ptr<ISensorDevice> sharedDevice(device, [](ISensorDevice*) {});
    SensorObserver observerAcceleration(&registry, AndroidSensor::ACCELERATION);
    SensorObserver observerProximity(&registry, AndroidSensor::PROXIMITY);

    // Create mock callbacks
    MockSensorCallback mockCallbackAcceleration;
    MockSensorCallback mockCallbackProximity;

    // Set up expectations: both callbacks should be called
    EXPECT_CALL(mockCallbackAcceleration, onSensorChanged(Eq(SensorData{1.0f, 2.0f, 3.0f})))
            .Times(1);
    EXPECT_CALL(mockCallbackProximity, onSensorChanged(Eq(SensorData{1.0f}))).Times(1);

    // Register the mock callbacks with the observer
    observerAcceleration.addCallback([&mockCallbackAcceleration](const SensorData& data) {
        mockCallbackAcceleration.onSensorChanged(data);
    });
    observerProximity.addCallback([&mockCallbackProximity](const SensorData& data) {
        mockCallbackProximity.onSensorChanged(data);
    });

    // Change the sensor data
    setAcceleration(1.0f, 2.0f, 3.0f);
    setProximity(1.0f);
}

}  // namespace goldfish::devices::sensor
