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

#include "goldfish/avd_universe/guest_status/guest_status.h"

namespace goldfish::avd_universe::guest_status {

archive::IWriter& operator<<(archive::IWriter& w, const GuestStatus& val) {
    w << val.reset << val.bootcomplete << val.heartbeat;
    return w;
}

absl::Status ReadValue(archive::IReader& r, GuestStatus& val) {
    return ReadValue(r, val.reset, val.bootcomplete, val.heartbeat);
}

void GuestStatus::OnPostLoad() {
    reset.OnPostLoad();
    bootcomplete.OnPostLoad();
    heartbeat.OnPostLoad();
}

}  // namespace goldfish::avd_universe::guest_status
