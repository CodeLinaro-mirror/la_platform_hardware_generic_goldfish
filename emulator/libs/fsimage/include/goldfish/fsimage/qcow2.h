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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include "goldfish/file/storage_capacity.h"

/*
 * We have our own code to manipulate qcow2 files because bdrv_img_create (libblock)
 * has too many build dependencies and requires complicated runtime initialization.
 *
 * The other option is to use `qemu-img` (external binary), but it is also messy:
 * requires shipping this binary, starting it and parsing the output.
 */

namespace goldfish::fsimage {

namespace fs = std::filesystem;

struct Qcow2Snapshot {
    std::string id;
    std::string name;
    uint64_t vm_state_size = 0;
    uint64_t disk_size = 0;
    absl::Time date;
    absl::Duration vm_clock;
    std::optional<uint64_t> icount;

    bool operator==(const Qcow2Snapshot&) const = default;
};

bool IsQcow2Path(const fs::path&);

absl::Status CreateQcow2Overlay(const fs::path& qcow2_image, const fs::path& backing_image);
absl::Status CreateQcow2Overlay(const fs::path& qcow2_image,
                                android::base::StorageCapacity raw_size);

absl::StatusOr<std::string> ReadBackingFileFromQcow2(const fs::path& qcow2_path);

absl::StatusOr<std::vector<Qcow2Snapshot>> ListQcow2Snapshots(const fs::path& qcow2_path);

}  // namespace goldfish::fsimage
