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
    case Battery::Status::Charging:
        return POWER_SUPPLY_STATUS_CHARGING;
    case Battery::Status::Discharging:
        return POWER_SUPPLY_STATUS_DISCHARGING;
    case Battery::Status::NotCharging:
        return POWER_SUPPLY_STATUS_NOT_CHARGING;
    case Battery::Status::Full:
        return POWER_SUPPLY_STATUS_FULL;
    default:
        return POWER_SUPPLY_STATUS_UNKNOWN;
    }
}

int BatteryHealthToQemu(Battery::Health health) {
    switch (health) {
    case Battery::Health::Good:
        return POWER_SUPPLY_HEALTH_GOOD;
    case Battery::Health::Failed:
        return POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
    case Battery::Health::Dead:
        return POWER_SUPPLY_HEALTH_DEAD;
    case Battery::Health::Overvoltage:
        return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
    case Battery::Health::Overheated:
        return POWER_SUPPLY_HEALTH_OVERHEAT;
    default:
        return POWER_SUPPLY_HEALTH_UNKNOWN;
    }
}

Battery::Status QemuToBatteryStatus(int status) {
    switch (status) {
    case POWER_SUPPLY_STATUS_CHARGING:
        return Battery::Status::Charging;
    case POWER_SUPPLY_STATUS_DISCHARGING:
        return Battery::Status::Discharging;
    case POWER_SUPPLY_STATUS_NOT_CHARGING:
        return Battery::Status::NotCharging;
    case POWER_SUPPLY_STATUS_FULL:
        return Battery::Status::Full;
    default:
        return Battery::Status::Unknown;
    }
}

Battery::Health QemuToBatteryHealth(int health) {
    switch (health) {
    case POWER_SUPPLY_HEALTH_GOOD:
        return Battery::Health::Good;
    case POWER_SUPPLY_HEALTH_DEAD:
        return Battery::Health::Dead;
    case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
        return Battery::Health::Overvoltage;
    case POWER_SUPPLY_HEALTH_OVERHEAT:
        return Battery::Health::Overheated;
    default:
        return Battery::Health::Failed;
    }
}

void ApplyStateToQemu(const Battery& state) {
    VLOG(1) << "Setting goldfish battery state to: " << state;
    // Presence
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_HAS_BATTERY, state.has_battery);
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_PRESENT, state.is_present);

    // Charger & Status
    goldfish_battery_set_prop(1, POWER_SUPPLY_PROP_ONLINE, state.charger != Battery::Charger::None);
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_STATUS, BatteryStatusToQemu(state.status));

    // Capacity
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_CAPACITY, state.charge_level);

    // Health
    goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_HEALTH, BatteryHealthToQemu(state.health));
}

}  // namespace

void RegisterBattery(ObservableBattery* observable_battery, bool is_present,
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
                    .charger = Battery::Charger::None,
                    .charge_level = 100,
                    .health = Battery::Health::Good,
                    .status = Battery::Status::Charging,
                });
            })
            .IgnoreError();

    // The subscription is alive for the duration of the emulator.
    static auto subscription = MakeScopedCallback(*observable_battery, [qemu_loop](Battery state) {
        qemu_loop->Post([state]() { ApplyStateToQemu(state); }).IgnoreError();
    });
}

}  // namespace goldfish::devices::battery
