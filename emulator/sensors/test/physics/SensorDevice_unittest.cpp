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

#include <android/base/testing/TestSystem.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/goldfish/config/avd-test.h"
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
        socket = goldfish::devices::fakeConnection(&looper);
        test_socket = (TestSocket*)socket.get();
        device = ISensorDevice::create(std::move(socket), avd(), &looper);
        clear();
    }

  public:
    void receive(std::string_view msg) { device->onReceive(msg.data(), msg.size()); }
    void clear() { test_socket->storage.clear(); }

  protected:
    TestLooper looper;
    TestSocket* test_socket;
    SocketPtr socket;
    std::shared_ptr<ISensorDevice> device;
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
    looper.setVirtualTimeNs(1234567890);
    receive("set-delay:10");
    EXPECT_THAT(test_socket->storage, HasSubstr("0015guest-sync:1234567890000"));
}

TEST_F(SensorDeviceTest, setTimeOffset) {
    looper.setVirtualTimeNs(1234567890);
    receive("time:100");
    receive("set-delay:1");
    EXPECT_THAT(test_socket->storage, HasSubstr("000Eguest-sync:10"));
}

TEST_F(SensorDeviceTest, timeKeepsOnRolling) {
    looper.setVirtualTimeNs(1234567890);
    receive("set-delay:1");
    clear();
    EXPECT_THAT(test_socket->storage, Eq(""));
    looper.runWithTimeoutMs(50);

    // We should see a sync several times.
    EXPECT_THAT(countOccurrences(test_socket->storage, "guest-sync:"), Gt(10));
}

}  // namespace goldfish::devices::sensor