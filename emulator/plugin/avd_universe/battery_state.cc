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

#include "goldfish/avd_universe/battery/battery_state.h"

#include <cstdint>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/status/status_macros.h"

namespace goldfish::avd_universe::battery {
namespace {

template <typename EnumT>
absl::Status ReadBatteryEnum(archive::IReader& r, EnumT min_val, EnumT max_val,
                             std::string_view name, EnumT& val) {
    int8_t loaded = 0;
    RETURN_IF_ERROR(ReadValue(r, loaded));
    if (loaded < static_cast<int8_t>(min_val) || loaded > static_cast<int8_t>(max_val)) {
        return absl::InvalidArgumentError(
                absl::StrCat("Invalid ", name, " value ", static_cast<int>(loaded),
                             " in snapshot (expected ", static_cast<int>(min_val), "..",
                             static_cast<int>(max_val), ")"));
    }
    val = static_cast<EnumT>(loaded);
    return absl::OkStatus();
}

}  // namespace

archive::IWriter& operator<<(archive::IWriter& w, const Battery::Status val) {
    w << static_cast<int8_t>(val);
    return w;
}

archive::IWriter& operator<<(archive::IWriter& w, const Battery::Charger val) {
    w << static_cast<int8_t>(val);
    return w;
}

archive::IWriter& operator<<(archive::IWriter& w, const Battery::Health val) {
    w << static_cast<int8_t>(val);
    return w;
}

archive::IWriter& operator<<(archive::IWriter& w, const Battery& val) {
    w << val.has_battery << val.is_present << val.charger << val.charge_level << val.health
      << val.status;
    return w;
}

absl::Status ReadValue(archive::IReader& r, Battery::Status& val) {
    return ReadBatteryEnum(r, Battery::Status::kUnknown, Battery::Status::kFull, "Battery::Status",
                           val);
}

absl::Status ReadValue(archive::IReader& r, Battery::Charger& val) {
    return ReadBatteryEnum(r, Battery::Charger::kNone, Battery::Charger::kWireless,
                           "Battery::Charger", val);
}

absl::Status ReadValue(archive::IReader& r, Battery::Health& val) {
    return ReadBatteryEnum(r, Battery::Health::kGood, Battery::Health::kOverheated,
                           "Battery::Health", val);
}

absl::Status ReadValue(archive::IReader& r, Battery& val) {
    Battery loaded;
    RETURN_IF_ERROR(ReadValue(r, loaded.has_battery, loaded.is_present, loaded.charger,
                              loaded.charge_level, loaded.health, loaded.status));
    if (loaded.charge_level < 0 || loaded.charge_level > 100) {
        return absl::InvalidArgumentError(absl::StrCat(
                "Invalid Battery::charge_level value ", loaded.charge_level,
                " in snapshot (expected 0..100)"));
    }
    val = loaded;
    return absl::OkStatus();
}

}  // namespace goldfish::avd_universe::battery
