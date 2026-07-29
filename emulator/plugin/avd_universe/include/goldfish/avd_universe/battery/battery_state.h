// Copyright 2025 The Android Open Source Project
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
#pragma once

#include <cstdint>
#include <string_view>

#include "absl/strings/str_format.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"
#include "goldfish/eventing/observable_value.h"

namespace goldfish::avd_universe::battery {

/**
 * @brief Represents the state of the emulated battery.
 *
 * This struct encapsulates all relevant battery information, including its
 * presence, charging status, health, and current charge level.
 */
struct Battery {
    /**
     * @brief Battery charging status.
     */
    enum class Status : int8_t {
        kUnknown = 0,
        kCharging = 1,
        kDischarging = 2,
        kNotCharging = 3,
        kFull = 4,
    };

    /**
     * @brief Types of battery chargers.
     */
    enum class Charger : int8_t {
        kNone = 0,
        kAc = 1,
        kUsb = 2,
        kWireless = 3,
    };

    /**
     * @brief Battery health states.
     */
    enum class Health : int8_t {
        kGood = 0,
        kFailed = 1,
        kDead = 2,
        kOvervoltage = 3,
        kOverheated = 4,
    };

    bool has_battery{true};
    bool is_present{true};
    Charger charger{Charger::kAc};
    int32_t charge_level{100};
    Health health{Health::kGood};
    Status status{Status::kCharging};

    template <typename Sink>
    friend void AbslStringify(Sink& sink, Status s) {
        switch (s) {
        case Status::kCharging:
            sink.Append("Charging");
            break;
        case Status::kDischarging:
            sink.Append("Discharging");
            break;
        case Status::kNotCharging:
            sink.Append("NotCharging");
            break;
        case Status::kFull:
            sink.Append("Full");
            break;
        default:
            sink.Append("Unknown");
            break;
        }
    }

    template <typename Sink>
    friend void AbslStringify(Sink& sink, Charger c) {
        switch (c) {
        case Charger::kAc:
            sink.Append("AC");
            break;
        case Charger::kUsb:
            sink.Append("USB");
            break;
        case Charger::kWireless:
            sink.Append("Wireless");
            break;
        default:
            sink.Append("None");
            break;
        }
    }

    template <typename Sink>
    friend void AbslStringify(Sink& sink, Health h) {
        switch (h) {
        case Health::kGood:
            sink.Append("Good");
            break;
        case Health::kFailed:
            sink.Append("Failed");
            break;
        case Health::kDead:
            sink.Append("Dead");
            break;
        case Health::kOvervoltage:
            sink.Append("Overvoltage");
            break;
        case Health::kOverheated:
            sink.Append("Overheated");
            break;
        default:
            sink.Append("Unknown");
            break;
        }
    }

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Battery& b) {
        absl::Format(&sink,
                     "Battery(has_battery=%s, is_present=%s, charger=%v, level=%d%%, health=%v, "
                     "status=%v)",
                     b.has_battery ? "true" : "false", b.is_present ? "true" : "false", b.charger,
                     b.charge_level, b.health, b.status);
    }
};

archive::IWriter& operator<<(archive::IWriter&, Battery::Status);
archive::IWriter& operator<<(archive::IWriter&, Battery::Charger);
archive::IWriter& operator<<(archive::IWriter&, Battery::Health);
archive::IWriter& operator<<(archive::IWriter&, const Battery&);

absl::Status ReadValue(archive::IReader&, Battery::Status&);
absl::Status ReadValue(archive::IReader&, Battery::Charger&);
absl::Status ReadValue(archive::IReader&, Battery::Health&);
absl::Status ReadValue(archive::IReader&, Battery&);

using ObservableBattery =
        eventing::ObservableValue<Battery, eventing::ObservableValueTriggerAlways>;

}  // namespace goldfish::avd_universe::battery
