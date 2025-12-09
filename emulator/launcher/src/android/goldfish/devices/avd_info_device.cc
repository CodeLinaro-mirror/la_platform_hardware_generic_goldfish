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

#include "emulator/launcher/src/android/goldfish/devices/avd_info_device.h"

#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace android::goldfish {

absl::Status AvdInfoDevice::initialize(const EmulatorConfig& emulator) {
    std::vector<std::pair<std::string, std::string>> params{
        {"serial_number", absl::StrCat(emulator.serial_number())},
        {"adb_port", absl::StrCat(emulator.adb_port())},
        {"avd_name", emulator.avd().display_name()},
        {"avd_id", emulator.avd().id()},
        {"avd_abi", emulator.avd().abi()},
        {"avd_api", absl::StrCat(emulator.avd().apiLevel())},
        {"avd_type", absl::StrCat(static_cast<int32_t>(emulator.avd().getDeviceType()))},
        {"avd_dir", emulator.avd().getContentPath().string()},
        {"build_sdk", emulator.avd().build_sdk()},
        {"build_id", emulator.avd().build_id()},
        {"build_flavour", emulator.avd().build_flavour()},
    };

    mAvdParams = absl::StrJoin(params, ",", [](std::string* s, const auto& pair) {
        absl::StrAppend(s, pair.first, "=", pair.second);
    });

    if (char* quit_after_boot = emulator.opts().quit_after_boot) {
        if (int timeout; absl::SimpleAtoi(quit_after_boot, &timeout)) {
            absl::StrAppend(&mAvdParams, ",quit_after_boot_timeout=", timeout);
        } else {
            return absl::InvalidArgumentError(absl::StrCat(
                    "Failed to parse -quit-after-boot parameter as int: ", quit_after_boot));
        }
    }
    return absl::OkStatus();
}

std::vector<std::string> AvdInfoDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    return {"-device", absl::StrCat("avdstart,", mAvdParams)};
}

}  // namespace android::goldfish
