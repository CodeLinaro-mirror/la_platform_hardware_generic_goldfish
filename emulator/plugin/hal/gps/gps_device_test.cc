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

#include <memory>

#include "absl/time/time.h"

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/test_connector_registry.h"

namespace goldfish::devices::gps {

using async::testing::TestEventLoop;
using goldfish::avd_universe::gps::Location;
using ::testing::IsEmpty;
using ::testing::MatchesRegex;

class GpsDeviceTest : public ::testing::Test {
    void SetUp() override {
        IGpsDevice::RegisterDevice(&location, &registry, mQemuLoop.get());
        device = registry.ConstructHalDevice<IGpsDevice>();
        test_socket = registry.HalSocket();
    }

    void TearDown() override {
        mQemuLoop->Post([this] { registry.Close(); }).IgnoreError();
        mQemuLoop->RunAll();
    }

  public:
    void clear() { test_socket->storage.clear(); }

  protected:
    const std::unique_ptr<TestEventLoop> mQemuLoop = TestEventLoop::Create();
    ObservableLocation location;
    TestConnectorRegistry registry;
    IGpsDevice* device = nullptr;
    TestHalSocket* test_socket = nullptr;
};

TEST_F(GpsDeviceTest, canCreateDevice) {
    EXPECT_NE(device, nullptr);
}

TEST_F(GpsDeviceTest, canSendLocation) {
    const Location kAmsterdam = {
        .latitude = 52.3676,
        .longitude = 4.9041,
        .speed = 0.0,     // Default speed
        .bearing = 0.0,   // Default bearing
        .altitude = 0.0,  // Default altitude
        .satellites = 0,  // Default satellites
    };

    location.SetValue(kAmsterdam);
    EXPECT_THAT(test_socket->storage, IsEmpty());

    mQemuLoop->RunAll();
    EXPECT_THAT(
            test_socket->storage,
            MatchesRegex(
                    R"(0043\$GPGGA,.*,5222.0560,N,00454.2459,E,1,00,1.0,0.00,M,0.0,M,,.*\s*0044\$GPRMC,.*,A,5222.0560,N,00454.2459,E,0.00,0.00,.*,0.0,W.*\s*)"));
}

TEST_F(GpsDeviceTest, periodicTimerSendsLocationOnQemuLoopAndStopsOnClose) {
    mQemuLoop->Post([this] { device->OnConnect(); }).IgnoreError();
    mQemuLoop->RunAll();
    EXPECT_THAT(test_socket->storage, IsEmpty());

    mQemuLoop->AdvanceClock(absl::Seconds(1));
    EXPECT_THAT(
            test_socket->storage,
            MatchesRegex(
                    R"(0044\$GPGGA,.*,3725.3211,N,12205.0383,W,1,08,1.0,10.00,M,0.0,M,,.*\s*0044\$GPRMC,.*,A,3725.3211,N,12205.0383,W,0.00,0.00,.*,0.0,W.*\s*)"));

    clear();
    mQemuLoop->Post([this] { registry.Close(); }).IgnoreError();
    mQemuLoop->RunAll();

    // Advancing the clock or updating location after OnClose must not send packets.
    mQemuLoop->AdvanceClock(absl::Seconds(1));
    location.SetValue(Location{.latitude = 52.3676, .longitude = 4.9041});
    mQemuLoop->RunAll();
    EXPECT_THAT(test_socket->storage, IsEmpty());
}

}  // namespace goldfish::devices::gps
