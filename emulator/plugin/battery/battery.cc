// Copyright 2024 The Android Open Source Project
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

#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/battery/goldfish_battery.h"

namespace goldfish::devices::battery {

using avd_universe::battery::Battery;
using avd_universe::battery::ObservableBattery;

namespace {

int BatteryStatusToQemu(Battery::Status status) {
    switch (status) {
    case Battery::Status::kCharging:
        return POWER_SUPPLY_STATUS_CHARGING;
    case Battery::Status::kDischarging:
        return POWER_SUPPLY_STATUS_DISCHARGING;
    case Battery::Status::kNotCharging:
        return POWER_SUPPLY_STATUS_NOT_CHARGING;
    case Battery::Status::kFull:
        return POWER_SUPPLY_STATUS_FULL;
    default:
        return POWER_SUPPLY_STATUS_UNKNOWN;
    }
}

int BatteryHealthToQemu(Battery::Health health) {
    switch (health) {
    case Battery::Health::kGood:
        return POWER_SUPPLY_HEALTH_GOOD;
    case Battery::Health::kFailed:
        return POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
    case Battery::Health::kDead:
        return POWER_SUPPLY_HEALTH_DEAD;
    case Battery::Health::kOvervoltage:
        return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
    case Battery::Health::kOverheated:
        return POWER_SUPPLY_HEALTH_OVERHEAT;
    default:
        return POWER_SUPPLY_HEALTH_UNKNOWN;
    }
}

Battery::Status QemuToBatteryStatus(int status) {
    switch (status) {
    case POWER_SUPPLY_STATUS_CHARGING:
        return Battery::Status::kCharging;
    case POWER_SUPPLY_STATUS_DISCHARGING:
        return Battery::Status::kDischarging;
    case POWER_SUPPLY_STATUS_NOT_CHARGING:
        return Battery::Status::kNotCharging;
    case POWER_SUPPLY_STATUS_FULL:
        return Battery::Status::kFull;
    default:
        return Battery::Status::kUnknown;
    }
}

Battery::Health QemuToBatteryHealth(int health) {
    switch (health) {
    case POWER_SUPPLY_HEALTH_GOOD:
        return Battery::Health::kGood;
    case POWER_SUPPLY_HEALTH_DEAD:
        return Battery::Health::kDead;
    case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
        return Battery::Health::kOvervoltage;
    case POWER_SUPPLY_HEALTH_OVERHEAT:
        return Battery::Health::kOverheated;
    default:
        return Battery::Health::kFailed;
    }
}

void ApplyStateToQemu(const Battery& state) {
    VLOG(1) << "Setting goldfish battery state to: " << state;
    // Presence
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_HAS_BATTERY, state.has_battery);
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_PRESENT, state.is_present);

    // Charger & Status
    goldfish_battery_set_prop(1, POWER_SUPPLY_PROP_ONLINE,
                              state.charger != Battery::Charger::kNone);
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_STATUS, BatteryStatusToQemu(state.status));

    // Capacity
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_CAPACITY, state.charge_level);

    // Health
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_HEALTH, BatteryHealthToQemu(state.health));
}

}  // namespace

ObservableBattery::ScopedCallbackHandle RegisterBattery(ObservableBattery* observable_battery,
                                                        bool is_present,
                                                        async::EventLoop* qemu_loop) {
    DCHECK(observable_battery) << "observable_battery cannot be null.";
    DCHECK(qemu_loop) << "qemu_loop cannot be null.";

    // Initial synchronization
    // TODO: This does not handle snapshots.
    qemu_loop
            ->Post([is_present]() {
                VLOG(1) << "Initializing goldfish battery presence to: " << is_present;
                // Only update battery presence on startup.
                ApplyStateToQemu(Battery{
                    .has_battery = is_present,
                    .is_present = is_present,
                    .charger = Battery::Charger::kNone,
                    .charge_level = 100,
                    .health = Battery::Health::kGood,
                    .status = Battery::Status::kCharging,
                });
            })
            .IgnoreError();

    // The subscription is alive for the duration of the emulator.
    return MakeScopedCallback(*observable_battery, [qemu_loop](Battery state) {
        qemu_loop->Post([state]() { ApplyStateToQemu(state); }).IgnoreError();
    });
}

}  // namespace goldfish::devices::battery
