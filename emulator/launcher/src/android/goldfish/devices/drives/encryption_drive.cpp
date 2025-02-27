

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
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"

namespace android::goldfish {
absl::Status EncryptionDrive::initialize(const Emulator& emulator) {
    if (exists()) {
        return absl::OkStatus();
    }

    auto hw = emulator.avd().hw();

    fs::path userdata_dir = fs::path(hw.disk_dataPartition_path).parent_path();
    if (!fs::exists(userdata_dir)) {
        return absl::NotFoundError(
                "No user data directory was found. Make sure you are configuring the "
                "encryption drive after setting up the user data drive.");
    }

    hw.disk_encryptionKeyPartition_path = (userdata_dir / "encryptionkey.img").string();
    if (!fs::exists(hw.disk_systemPartition_initPath)) {
        return absl::NotFoundError(
                absl::StrFormat("Init path '%s' not found", hw.disk_systemPartition_initPath));
    }

    auto sysimage_dir = fs::path(hw.disk_systemPartition_initPath).parent_path();
    if (!fs::exists(sysimage_dir)) {
        return absl::NotFoundError(
                absl::StrCat("System image directory not found: ", sysimage_dir.string()));
    }

    auto init_encryptionkey_img_path = sysimage_dir / "encryptionkey.img";
    if (!fs::exists(init_encryptionkey_img_path)) {
        return absl::NotFoundError(absl::StrFormat("System encryption key image not found in: %s",
                                                   sysimage_dir.string()));
    }

    fs::copy_options options = fs::copy_options::overwrite_existing;
    fs::copy(init_encryptionkey_img_path, hw.disk_encryptionKeyPartition_path, options);

    if (!fs::exists(hw.disk_encryptionKeyPartition_path)) {
        return absl::NotFoundError(absl::StrFormat("Failed to copy '%s' to '%s'",
                                                   init_encryptionkey_img_path.string(),
                                                   hw.disk_encryptionKeyPartition_path));
    }

    return convertImgToQcow2(hw.disk_encryptionKeyPartition_path);
}

}  // namespace android::goldfish