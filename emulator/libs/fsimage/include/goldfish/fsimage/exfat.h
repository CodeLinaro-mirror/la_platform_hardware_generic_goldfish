// Copyright 2026 The Android Open Source Project
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

#include <filesystem>
#include <string_view>

#include "absl/status/status.h"

#include "goldfish/file/storage_capacity.h"

namespace goldfish::fsimage {

namespace fs = std::filesystem;

absl::Status CreateEmptyExFATImage(const fs::path& image, android::base::StorageCapacity size,
                                   std::string_view volume_label = "");

absl::Status CreateExFATImageFromDirectory(const fs::path& image, const fs::path& source_directory,
                                           android::base::StorageCapacity size,
                                           std::string_view volume_label = "");

}  // namespace goldfish::fsimage
