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
#include "android/gps/GpsDevice.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/base/testing/TestSystem.h"
#include "android/goldfish/config/fake-avd.h"
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
        mClientLoop = TestEventLoop::create();
        mQemuLoop = TestEventLoop::create();

        IGpsDevice::registerDevice(&location, &registry, mClientLoop.get(), mQemuLoop.get());
        device = registry.constructHalDevice<IGpsDevice>();
        test_socket = registry.halSocket();
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

    location.setValue(kAmsterdam);
    EXPECT_THAT(test_socket->storage,
#ifdef _WIN32
                MatchesRegex(R"(0039\$GnssRpcV1,0,52\.3676,4\.9041,0,0,1,0,\d+,0\.5,2,0)"));
#else
                MatchesRegex(R"(0039\$GnssRpcV1,0,52\.3676,4\.9041,0,0,1,0,[0-9]+,0\.5,2,0)"));
#endif
}

}  // namespace goldfish::devices::gps
