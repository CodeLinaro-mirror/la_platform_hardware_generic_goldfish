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

#include "goldfish/avd_universe/gps/location.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/status/status_macros.h"

namespace goldfish::avd_universe::gps {

archive::IWriter& operator<<(archive::IWriter& w, const Location& val) {
    w << val.latitude << val.longitude << val.speed << val.bearing << val.altitude
      << val.satellites;

    return w;
}

absl::Status ReadValue(archive::IReader& r, Location& val) {
    Location loaded;
    RETURN_IF_ERROR(ReadValue(r, loaded.latitude, loaded.longitude, loaded.speed, loaded.bearing,
                              loaded.altitude, loaded.satellites));
    if (loaded.satellites < 0) {
        return absl::InvalidArgumentError(absl::StrCat(
                "Negative satellite count (", loaded.satellites,
                ", expected >= 0) in Location snapshot"));
    }
    val = loaded;
    return absl::OkStatus();
}

}  // namespace goldfish::avd_universe::gps
