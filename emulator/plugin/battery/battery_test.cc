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

#include "goldfish/devices/battery/battery.h"

#include <gtest/gtest.h>

#include <utility>

#include "absl/container/flat_hash_map.h"

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/avd_universe/battery/battery_state.h"
#include "goldfish/battery/goldfish_battery.h"

namespace {

absl::flat_hash_map<std::pair<int, int>, int>& BatteryProps() {
    static auto* const props = new absl::flat_hash_map<std::pair<int, int>, int>();
    return *props;
}

}  // namespace

extern "C" void goldfish_battery_set_prop(int ac, int property, int value) {
    BatteryProps()[{ac, property}] = value;
}

namespace goldfish::devices::battery {
namespace {

using ::goldfish::avd_universe::battery::Battery;
using ::goldfish::avd_universe::battery::ObservableBattery;

class RegisterBatteryTest : public ::testing::Test {
  protected:
    void SetUp() override { BatteryProps().clear(); }

    static int GetProp(int ac, int property) {
        auto it = BatteryProps().find({ac, property});
        return it != BatteryProps().end() ? it->second : -1;
    }

    ObservableBattery observable_battery_;
    std::unique_ptr<async::testing::TestEventLoop> loop_ = async::testing::TestEventLoop::Create();
};

TEST_F(RegisterBatteryTest, InitializesAcChargerOnlineWhenPresent) {
    auto sub = RegisterBattery(&observable_battery_, /*is_present=*/true, loop_.get());
    loop_->RunAll();

    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_HAS_BATTERY), 1);
    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_PRESENT), 1);
    EXPECT_EQ(GetProp(1, POWER_SUPPLY_PROP_ONLINE), 1);
    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_STATUS), POWER_SUPPLY_STATUS_CHARGING);
    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_CAPACITY), 100);
    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_HEALTH), POWER_SUPPLY_HEALTH_GOOD);
}

TEST_F(RegisterBatteryTest, InitializesNotPresentWhenDisabled) {
    auto sub = RegisterBattery(&observable_battery_, /*is_present=*/false, loop_.get());
    loop_->RunAll();

    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_HAS_BATTERY), 0);
    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_PRESENT), 0);
    EXPECT_EQ(GetProp(1, POWER_SUPPLY_PROP_ONLINE), 1);
}

TEST_F(RegisterBatteryTest, ForwardsObservableBatteryUpdatesToQemu) {
    auto sub = RegisterBattery(&observable_battery_, /*is_present=*/true, loop_.get());
    loop_->RunAll();

    observable_battery_.SetValue(Battery{
        .has_battery = true,
        .is_present = true,
        .charger = Battery::Charger::kNone,
        .charge_level = 42,
        .health = Battery::Health::kOverheated,
        .status = Battery::Status::kDischarging,
    });
    loop_->RunAll();

    EXPECT_EQ(GetProp(1, POWER_SUPPLY_PROP_ONLINE), 0);
    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_STATUS), POWER_SUPPLY_STATUS_DISCHARGING);
    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_CAPACITY), 42);
    EXPECT_EQ(GetProp(0, POWER_SUPPLY_PROP_HEALTH), POWER_SUPPLY_HEALTH_OVERHEAT);
}

}  // namespace
}  // namespace goldfish::devices::battery
