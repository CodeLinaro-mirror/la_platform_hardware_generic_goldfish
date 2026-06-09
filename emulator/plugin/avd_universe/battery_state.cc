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

#include "android/status/status_macros.h"

namespace goldfish::avd_universe::battery {

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
    int8_t loaded = 0;
    RETURN_IF_ERROR(ReadValue(r, loaded));
    val = static_cast<Battery::Status>(loaded);
    return absl::OkStatus();
}

absl::Status ReadValue(archive::IReader& r, Battery::Charger& val) {
    int8_t loaded = 0;
    RETURN_IF_ERROR(ReadValue(r, loaded));
    val = static_cast<Battery::Charger>(loaded);
    return absl::OkStatus();
}

absl::Status ReadValue(archive::IReader& r, Battery::Health& val) {
    int8_t loaded = 0;
    RETURN_IF_ERROR(ReadValue(r, loaded));
    val = static_cast<Battery::Health>(loaded);
    return absl::OkStatus();
}

absl::Status ReadValue(archive::IReader& r, Battery& val) {
    return ReadValue(r, val.has_battery, val.is_present, val.charger, val.charge_level, val.health,
                     val.status);
}

}  // namespace goldfish::avd_universe::battery
