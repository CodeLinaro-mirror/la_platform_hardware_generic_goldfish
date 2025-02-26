

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
#include <android/goldfish/config/hardware_config.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "disk_drive.h"

namespace android::goldfish {

class UserDataDrive : public MutableDiskDrive {
  public:
    explicit UserDataDrive(const HardwareConfig& hw) : MutableDiskDrive("userdata", "05.0") {
        mDiskId = "userdata";
        mDiskImage = fs::path(absl::StrCat(hw.disk_dataPartition_path, ".qcow2"));
    }

    absl::Status initialize(const Emulator& emulator) override;

  private:
    absl::Status minimizeUserDataPartition(const Emulator& emulator);
    absl::Status createImage(const HardwareConfig& hw, const fs::path data_path);
    absl::Status createUserData(const Emulator& emulator, const fs::path data_path, bool asQcow2);
};
}  // namespace android::goldfish
