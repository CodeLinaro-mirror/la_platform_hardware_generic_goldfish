// Copyright 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#pragma once

#include <cstdint>
#include <filesystem>

#include "absl/status/status.h"

#include "goldfish/file/storage_capacity.h"

namespace goldfish::fsimage {

namespace fs = std::filesystem;

absl::Status CreateEmptyExt4Image(const fs::path& image, android::base::StorageCapacity size,
                                  const std::string& mount_point);

absl::Status CreateExt4ImageFromDir(const fs::path& image, const fs::path& src_dir,
                                    android::base::StorageCapacity size,
                                    const std::string& mount_point);

}  // namespace goldfish::fsimage
