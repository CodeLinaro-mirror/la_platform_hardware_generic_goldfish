

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
#include "encryption_drive.h"

#include <filesystem>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "aemu/base/utils/status_macros.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"

namespace android::goldfish {

absl::Status EncryptionDrive::initialize(const Emulator& emulator) {
    if (exists()) {
        return absl::OkStatus();
    }

    ASSIGN_OR_RETURN(auto init_encryptionkey_img_path,
                     emulator.avd().getSystemImageFilePath(Avd::ImageType::ENCRYPTIONKEY));

    auto hw = emulator.avd().hw();
    if (!fs::exists(hw.disk_encryptionKeyPartition_path)) {
        fs::copy_options options = fs::copy_options::overwrite_existing;
        fs::copy(init_encryptionkey_img_path, hw.disk_encryptionKeyPartition_path, options);

        if (!fs::exists(hw.disk_encryptionKeyPartition_path)) {
            return absl::NotFoundError(absl::StrFormat("Failed to copy '%s' to '%s'",
                                                       init_encryptionkey_img_path.string(),
                                                       hw.disk_encryptionKeyPartition_path));
        }
    }

    return convertImgToQcow2(hw.disk_encryptionKeyPartition_path);
}

}  // namespace android::goldfish
