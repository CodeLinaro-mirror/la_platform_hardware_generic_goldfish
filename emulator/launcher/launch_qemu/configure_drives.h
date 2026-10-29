// Copyright (C) 2024 The Android Open Source Project
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

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "android/cmdline_option.h"
#include "android/goldfish/avd.h"
#include "android/status/status_macros.h"
#include "disk_drive.h"
#include "emulator_config.h"
#include "goldfish/file/file.h"

namespace android::goldfish {

namespace internal {
absl::StatusOr<std::vector<std::unique_ptr<PciDevice>>> getDiskConfigs(const Avd& avd,
                                                                       const AndroidOptions& opts);
}  // namespace internal

// Returns whether the userdata partition of the guest is new: the userdata drive creates a new
// qcow2 overlay, which holds all the changes of the guest, when it is missing or invalid. This is
// the case on the first boot of the AVD, and after -wipe-data or the Wipe Data action of Android
// Studio, which deletes the overlay but keeps userdata.img.
bool IsUserdataNew(const Avd& avd);

// Templated for simplified testing.
template <typename T>
absl::Status addDrives(const EmulatorConfig& config, T& emulator) {
    ASSIGN_OR_RETURN(auto disks, internal::getDiskConfigs(config.avd(), config.opts()));

    for (std::unique_ptr<PciDevice>& disk : disks) {
        emulator.addDevice(std::move(disk));
    }

    return absl::OkStatus();
}

}  // namespace android::goldfish
