

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
#include "android/goldfish/devices/drives/cache_drive.h"

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aemu/base/logging/Log.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"

#include <filesystem>

namespace android::goldfish {
absl::Status CacheDrive::initialize(const Emulator& emulator) {
  if (exists()) {
    return absl::OkStatus();
  }

  LOG(INFO) << "Preparing empty cache drive";
  auto hw = emulator.avd().hw();
  auto status = createExt4Image(hw.disk_cachePartition_path,
                                     hw.disk_cachePartition_size, "cache");
  if (!status.ok()) {
    return status;
  }

  return convertImgToQcow2(hw.disk_cachePartition_path);
}

} // namespace android::goldfish