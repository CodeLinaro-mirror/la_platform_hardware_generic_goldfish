// Copyright 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "absl/status/status_matchers.h"
#include "absl/time/time.h"

#include "goldfish/archive/deque_archive.h"
#include "goldfish/avd_universe/battery/battery_state.h"
#include "goldfish/avd_universe/gps/location.h"
#include "goldfish/avd_universe/guest_status/guest_status.h"

namespace goldfish::avd_universe {
namespace {

using ::absl_testing::IsOk;
using ::testing::Not;

TEST(BatterySnapshotTest, ValidRoundTripAndCorruptedInputsRejected) {
    battery::Battery b{
        .has_battery = true,
        .is_present = true,
        .charger = battery::Battery::Charger::kUsb,
        .charge_level = 85,
        .health = battery::Battery::Health::kGood,
        .status = battery::Battery::Status::kDischarging,
    };

    archive::DequeArchive archive;
    archive << b;

    battery::Battery loaded;
    ASSERT_THAT(ReadValue(archive, loaded), IsOk());
    EXPECT_EQ(loaded.charger, b.charger);
    EXPECT_EQ(loaded.charge_level, b.charge_level);
    EXPECT_EQ(loaded.health, b.health);
    EXPECT_EQ(loaded.status, b.status);

    // Out-of-range charge_level rejected
    for (const int32_t bad_charge : {-1, 101}) {
        archive::DequeArchive bad_archive;
        bad_archive << true << true << int8_t{1} << bad_charge << int8_t{0} << int8_t{1};
        battery::Battery dst;
        EXPECT_THAT(ReadValue(bad_archive, dst), Not(IsOk()));
    }

    // Out-of-range Charger enum rejected
    for (const int8_t bad_charger : {int8_t{-1}, int8_t{4}}) {
        archive::DequeArchive bad_archive;
        bad_archive << true << true << bad_charger << int32_t{50} << int8_t{0} << int8_t{1};
        battery::Battery dst;
        EXPECT_THAT(ReadValue(bad_archive, dst), Not(IsOk()));
    }

    // Out-of-range Health enum rejected
    for (const int8_t bad_health : {int8_t{-1}, int8_t{5}}) {
        archive::DequeArchive bad_archive;
        bad_archive << true << true << int8_t{1} << int32_t{50} << bad_health << int8_t{1};
        battery::Battery dst;
        EXPECT_THAT(ReadValue(bad_archive, dst), Not(IsOk()));
    }

    // Out-of-range Status enum rejected
    for (const int8_t bad_status : {int8_t{-1}, int8_t{5}}) {
        archive::DequeArchive bad_archive;
        bad_archive << true << true << int8_t{1} << int32_t{50} << int8_t{0} << bad_status;
        battery::Battery dst;
        EXPECT_THAT(ReadValue(bad_archive, dst), Not(IsOk()));
    }
}

TEST(GuestStatusSnapshotTest, ValidRoundTripAndCorruptedTimestampsRejected) {
    const absl::Time t0 = absl::FromUnixSeconds(1000);
    const absl::Time t1 = absl::FromUnixSeconds(1010);

    guest_status::GuestStatus gs;
    gs.Reset(t0);
    gs.SetBootComplete(t1);
    gs.Heartbeat();

    archive::DequeArchive archive;
    archive << gs;

    guest_status::GuestStatus loaded;
    ASSERT_THAT(ReadValue(archive, loaded), IsOk());
    EXPECT_TRUE(loaded.IsBootCompleted());
    EXPECT_EQ(loaded.GetBootCompleteDuration(), absl::Seconds(10));
    EXPECT_EQ(loaded.GetHeartbeatCounter(), 1U);

    // bootcompleteT_ < resetT_ (and != UnixEpoch) rejected
    {
        archive::DequeArchive bad_archive;
        bad_archive << t1 << t0 << uint64_t{1};
        guest_status::GuestStatus dst;
        EXPECT_THAT(ReadValue(bad_archive, dst), Not(IsOk()));
    }
}

TEST(LocationSnapshotTest, DefaultsToGoogleplex) {
    const gps::Location defaults;
    EXPECT_DOUBLE_EQ(defaults.latitude, gps::Location::kDefaultLatitude);
    EXPECT_DOUBLE_EQ(defaults.longitude, gps::Location::kDefaultLongitude);
    EXPECT_DOUBLE_EQ(defaults.speed, 0.0);
    EXPECT_DOUBLE_EQ(defaults.bearing, 0.0);
    EXPECT_DOUBLE_EQ(defaults.altitude, gps::Location::kDefaultAltitudeMeters);
    EXPECT_DOUBLE_EQ(defaults.accuracy, 0.0);
    EXPECT_EQ(defaults.satellites, gps::Location::kDefaultSatellites);
}

TEST(LocationSnapshotTest, ValidRoundTripAndCorruptedInputsRejected) {
    const gps::Location loc{
        .latitude = 37.422,
        .longitude = -122.084,
        .speed = 1.5,
        .bearing = 90.0,
        .altitude = 10.0,
        .accuracy = 4.5,
        .satellites = 8,
    };

    archive::DequeArchive archive;
    archive << loc;

    gps::Location loaded;
    ASSERT_THAT(ReadValue(archive, loaded), IsOk());
    EXPECT_DOUBLE_EQ(loaded.latitude, loc.latitude);
    EXPECT_DOUBLE_EQ(loaded.longitude, loc.longitude);
    EXPECT_DOUBLE_EQ(loaded.speed, loc.speed);
    EXPECT_DOUBLE_EQ(loaded.bearing, loc.bearing);
    EXPECT_DOUBLE_EQ(loaded.altitude, loc.altitude);
    EXPECT_DOUBLE_EQ(loaded.accuracy, loc.accuracy);
    EXPECT_EQ(loaded.satellites, loc.satellites);

    // Negative satellite count rejected
    {
        archive::DequeArchive bad_archive;
        bad_archive << 37.422 << -122.084 << 1.5 << 90.0 << 10.0 << 4.5 << int32_t{-1};
        gps::Location dst;
        EXPECT_THAT(ReadValue(bad_archive, dst), Not(IsOk()));
    }

    // Negative accuracy rejected
    {
        archive::DequeArchive bad_archive;
        bad_archive << 37.422 << -122.084 << 1.5 << 90.0 << 10.0 << -0.5 << int32_t{4};
        gps::Location dst;
        EXPECT_THAT(ReadValue(bad_archive, dst), Not(IsOk()));
    }

    // Out-of-bounds latitude / longitude rejected
    {
        archive::DequeArchive bad_lat;
        bad_lat << 91.0 << -122.084 << 1.5 << 90.0 << 10.0 << 0.0 << int32_t{4};
        gps::Location dst;
        EXPECT_THAT(ReadValue(bad_lat, dst), Not(IsOk()));
    }
    {
        archive::DequeArchive bad_lon;
        bad_lon << 37.422 << -181.0 << 1.5 << 90.0 << 10.0 << 0.0 << int32_t{4};
        gps::Location dst;
        EXPECT_THAT(ReadValue(bad_lon, dst), Not(IsOk()));
    }

    // NaN / Inf coordinate rejected
    {
        archive::DequeArchive bad_archive;
        bad_archive << std::numeric_limits<double>::quiet_NaN() << -122.084 << 1.5 << 90.0 << 10.0
                    << 0.0 << int32_t{4};
        gps::Location dst;
        EXPECT_THAT(ReadValue(bad_archive, dst), Not(IsOk()));
    }
    {
        archive::DequeArchive bad_inf;
        bad_inf << 37.422 << -122.084 << std::numeric_limits<double>::infinity() << 90.0 << 10.0
                << 0.0 << int32_t{4};
        gps::Location dst;
        EXPECT_THAT(ReadValue(bad_inf, dst), Not(IsOk()));
    }
}

}  // namespace
}  // namespace goldfish::avd_universe
