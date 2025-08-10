

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

#include <filesystem>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aemu/base/utils/status_macros.h"
#include "android/cmdline-option.h"
#include "android/goldfish/config/avd.h"
#include "disk_drive.h"

namespace android::goldfish {

namespace internal {
struct DiskConfig {
  std::string id;
  std::string pci_address;

  bool is_writable;

  std::optional<fs::path> system_image_path_ro;
  fs::path user_image_path;

  uint64_t size_bytes;
  bool wipe_existing;
};

absl::StatusOr<std::vector<DiskConfig>> getDiskConfigs(const Avd& avd, const AndroidOptions& opts);
}  // namespace internal

// Templated for simplified testing.
template <typename T>
absl::Status addDrives(T& emulator) {
  ASSIGN_OR_RETURN(auto disk_configs, internal::getDiskConfigs(emulator.avd(), emulator.opts()));

  for (const auto& dc : disk_configs) {
    // TODO(whollins): why don't we use qcow2 without backing file for sdcard and cache?
    // And why do we copy the images for encryption_key (and system and vendor in writable)?
    if (dc.is_writable) {
      auto qcow2 = fs::path(dc.user_image_path).concat(".qcow2");
      emulator.template addDevice<RwDrive>(dc.id, dc.pci_address, dc.system_image_path_ro,
                                           dc.user_image_path, qcow2, dc.size_bytes,
                                           dc.wipe_existing);
    } else {
      fs::path image;
      if (fs::exists(dc.user_image_path)) {
        image = dc.user_image_path;
      } else if (dc.system_image_path_ro) {
        image = *dc.system_image_path_ro;
      } else {
        LOG(FATAL) << "A read-only drive was requested with a non-existent image: "
                   << dc.user_image_path;
      }
      emulator.template addDevice<RoDrive>(dc.id, dc.pci_address, image);
    }
  }
  return absl::OkStatus();
}

}  // namespace android::goldfish