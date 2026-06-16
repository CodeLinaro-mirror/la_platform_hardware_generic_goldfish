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
#include "goldfish/devices/gps/gps_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>

#include "gmock/gmock.h"

#include "android/base/testing/test_system.h"
#include "goldfish//async/testing/test_event_loop.h"
#include "goldfish/devices/test_connector_registry.h"

namespace goldfish::devices::gps {

using android::base::TestSystem;
using async::testing::TestEventLoop;
using goldfish::avd_universe::gps::Location;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;

class GpsDeviceTest : public ::testing::Test {
    void SetUp() override {
        mClientLoop = TestEventLoop::Create();
        mQemuLoop = TestEventLoop::Create();

        IGpsDevice::RegisterDevice(&location, &registry, mClientLoop.get(), mQemuLoop.get());
        device = registry.ConstructHalDevice<IGpsDevice>();
        test_socket = registry.HalSocket();
    }

    void TearDown() override {
        registry.Close();
        mClientLoop->RunAll();
    }

  public:
    void clear() { test_socket->storage.clear(); }

  protected:
    ObservableLocation location;
    TestConnectorRegistry registry;
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;
    IGpsDevice* device;
    TestHalSocket* test_socket;
};

TEST_F(GpsDeviceTest, canCreateDevice) {
    EXPECT_NE(device, nullptr);
}

TEST_F(GpsDeviceTest, canSendLocation) {
    Location kAmsterdam = {
        .latitude = 52.3676,
        .longitude = 4.9041,
        .speed = 0.0,     // Default speed
        .bearing = 0.0,   // Default bearing
        .altitude = 0.0,  // Default altitude
        .satellites = 0,  // Default satellites
    };

    location.SetValue(kAmsterdam);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    EXPECT_THAT(
            test_socket->storage,
            MatchesRegex(
                    R"(0043\$GPGGA,.*,5222.0560,N,00454.2459,E,1,00,1.0,0.00,M,0.0,M,,.*\s*0044\$GPRMC,.*,A,5222.0560,N,00454.2459,E,0.00,0.00,.*,0.0,W.*\s*)"));
}

}  // namespace goldfish::devices::gps
