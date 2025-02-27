

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
#include "sdcard_drive.h"

#include <filesystem>

#include "absl/status/status.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"

namespace android::goldfish {
absl::Status SDCardDrive::initialize(const Emulator& emulator) {
    if (exists()) {
        return absl::OkStatus();
    }

    auto hw = emulator.avd().hw();

    if (!fs::exists(hw.hw_sdCard_path)) {
        auto status = createExt4Image(hw.hw_sdCard_path, hw.hw_sdCard_size, "sdcard");
        if (!status.ok()) {
            return status;
        }
    }
    return convertImgToQcow2(hw.hw_sdCard_path);
}
}  // namespace android::goldfish