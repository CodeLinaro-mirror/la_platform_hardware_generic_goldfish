
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

#include "goldfish/battery/goldfish_battery.h"

namespace goldfish::devices::battery {
class Battery : public IBattery {
  public:
    Battery() = default;

    // Battery presence
    void setHasBattery(bool hasBattery) override {
        goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_HAS_BATTERY, hasBattery);
    }

    bool hasBattery() const override {
        return static_cast<bool>(goldfish_battery_read_prop(POWER_SUPPLY_PROP_HAS_BATTERY));
    }

    void setPresent(bool isPresent) override {
        goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_PRESENT, isPresent);
    }

    bool isPresent() const override {
        return static_cast<bool>(goldfish_battery_read_prop(POWER_SUPPLY_PROP_PRESENT));
    }

    // Charging state
    void setCharging(bool isCharging) override {
        goldfish_battery_set_prop(1, POWER_SUPPLY_PROP_ONLINE, isCharging);
    }

    bool isCharging() const override {
        return static_cast<bool>(goldfish_battery_read_prop(POWER_SUPPLY_PROP_ONLINE));
    }

    void setChargerType(ChargerType charger) override { setCharging(charger != ChargerType::NONE); }

    ChargerType getChargerType() const override {
        return (isCharging() ? ChargerType::AC : ChargerType::NONE);
    }

    // Charge level
    void setChargeLevel(int percentFull) override {
        if (percentFull < 0 || percentFull > 100) {
            return;
        }

        goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_CAPACITY, percentFull);
    }

    int getChargeLevel() const override {
        return goldfish_battery_read_prop(POWER_SUPPLY_PROP_CAPACITY);
    }

    // Health state
    void setHealth(Health health) override {
        int value;
        switch (health) {
        case Health::GOOD:
            value = POWER_SUPPLY_HEALTH_GOOD;
            break;
        case Health::FAILED:
            value = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
            break;
        case Health::DEAD:
            value = POWER_SUPPLY_HEALTH_DEAD;
            break;
        case Health::OVERVOLTAGE:
            value = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
            break;
        case Health::OVERHEATED:
            value = POWER_SUPPLY_HEALTH_OVERHEAT;
            break;
        case Health::UNKNOWN:
            value = POWER_SUPPLY_HEALTH_UNKNOWN;
            break;
        default:
            value = POWER_SUPPLY_HEALTH_UNKNOWN;
            break;
        }

        goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_HEALTH, value);
    }

    Health getHealth() const override {
        switch (goldfish_battery_read_prop(POWER_SUPPLY_PROP_HEALTH)) {
        case POWER_SUPPLY_HEALTH_GOOD:
            return Health::GOOD;
        case POWER_SUPPLY_HEALTH_UNSPEC_FAILURE:
            return Health::FAILED;
        case POWER_SUPPLY_HEALTH_DEAD:
            return Health::DEAD;
        case POWER_SUPPLY_HEALTH_OVERVOLTAGE:
            return Health::OVERVOLTAGE;
        case POWER_SUPPLY_HEALTH_OVERHEAT:
            return Health::OVERHEATED;
        case POWER_SUPPLY_HEALTH_UNKNOWN:
            return Health::UNKNOWN;
        default:
            return Health::UNKNOWN;
        }
    }

    // Status
    void setStatus(Status status) override {
        int value;
        switch (status) {
        case Status::UNKNOWN:
            value = POWER_SUPPLY_STATUS_UNKNOWN;
            break;
        case Status::CHARGING:
            value = POWER_SUPPLY_STATUS_CHARGING;
            break;
        case Status::DISCHARGING:
            value = POWER_SUPPLY_STATUS_DISCHARGING;
            break;
        case Status::NOT_CHARGING:
            value = POWER_SUPPLY_STATUS_NOT_CHARGING;
            break;
        case Status::FULL:
            value = POWER_SUPPLY_STATUS_FULL;
            break;
        default:
            value = POWER_SUPPLY_STATUS_UNKNOWN;
            break;
        }

        goldfish_battery_set_prop(0, POWER_SUPPLY_PROP_STATUS, value);
    }

    Status getStatus() const override {
        switch (goldfish_battery_read_prop(POWER_SUPPLY_PROP_STATUS)) {
        case POWER_SUPPLY_STATUS_UNKNOWN:
            return Status::UNKNOWN;
        case POWER_SUPPLY_STATUS_CHARGING:
            return Status::CHARGING;
        case POWER_SUPPLY_STATUS_DISCHARGING:
            return Status::DISCHARGING;
        case POWER_SUPPLY_STATUS_NOT_CHARGING:
            return Status::NOT_CHARGING;
        case POWER_SUPPLY_STATUS_FULL:
            return Status::FULL;
        default:
            return Status::UNKNOWN;
        }
    }
};

IBattery* instance() {
    static Battery battery;
    return &battery;
}
}  // namespace goldfish::devices::battery
