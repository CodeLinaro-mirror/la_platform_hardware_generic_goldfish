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

#include <cstdint>
#include <filesystem>
#include <string>

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "goldfish/file/storage_capacity.h"

namespace goldfish::fsimage {

namespace fs = std::filesystem;

/// Volume attributes used when formatting an exFAT image.
struct ExfatVolumeAttributes {
    std::string label;
    uint32_t serial_number = 1;
    absl::Time timestamp = absl::Now();
};

bool IsExFatFileImage(const fs::path& path);

absl::Status CreateEmptyExFATImage(const fs::path& image, android::base::StorageCapacity size,
                                   const ExfatVolumeAttributes& attrs = {});

/// Creates an exFAT image populated from @p source_directory.
/// If @p size is 0, sizes the image to fit @p source_directory.
absl::Status CreateExFATImageFromDirectory(const fs::path& image, const fs::path& source_directory,
                                           android::base::StorageCapacity size,
                                           const ExfatVolumeAttributes& attrs = {});

}  // namespace goldfish::fsimage
