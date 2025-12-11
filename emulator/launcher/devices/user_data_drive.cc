

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

#include "user_data_drive.h"

#include <filesystem>
#include <fstream>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "aemu/base/utils/status_macros.h"
#include "android/base/file/file.h"
#include "android/base/storage_capacity.h"
#include "android/filesystems/ext4_resize.h"
#include "android/filesystems/ext4_utils.h"

namespace android::goldfish {

namespace fs = std::filesystem;

namespace {

using android::base::StorageCapacity;
using android::base::operator""_MiB;
using android::base::operator""_TiB;

absl::Status resizePartition(fs::path partition, StorageCapacity size) {
    constexpr auto minSize = 128_MiB;
    constexpr auto maxSize = 16_TiB;

    if (size < minSize) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Partition '%s' cannot be smaller than %s. Requested size: %s",
                                partition.string(), minSize.string(), size.string()));
    }

    if (size > maxSize) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Partition '%s' cannot be larger than %s. Requested size: %s",
                                partition.string(), maxSize.string(), size.string()));
    }

    // TODO the extprogs are not currently bundled with emu-next. For this to work they should be
    // included in the release zip.
    int resizeResult = resizeExt4Partition(fs::path("some-dir-TODO"), partition.string().c_str(),
                                           size.bytes());

    // Interpret the error codes can propagate.
    if (resizeResult != 0) {
        std::string resizeError;
        switch (resizeResult) {
        case -1:
            resizeError = "Argument formatting failed";
            break;
        case -2:
            resizeError = "System call failed";
            break;
        default:
            resizeError = absl::StrFormat("resize2fs failed with exit code %d", resizeResult);
            break;
        }
        return absl::InternalError(absl::StrFormat("Could not resize partition %s. Error: %s",
                                                   partition.string(), resizeError));
    }

    return absl::OkStatus();
}

bool pathIsExt4(fs::path path) {
    // read 2 bytes
    uint8_t magic[2] = {'\0'};
    std::ifstream ifs(path, std::ios_base::binary);
    if (!ifs.good()) {
        return false;
    }
    ifs.ignore(1080);
    ifs.read(reinterpret_cast<char*>(magic), sizeof(magic));

    return magic[0] == 0x53 && magic[1] == 0xEF;
}

absl::Status minimizePartition(fs::path image, uint64_t desired_size_bytes) {
    if (pathIsExt4(image)) {
        ASSIGN_OR_RETURN(auto current_data_size, base::file::file_size(image));
        if (desired_size_bytes > 0 && current_data_size < desired_size_bytes) {
            // Log resize intent
            LOG(WARNING) << "Resizing userdata partition " << image << " from "
                         << current_data_size.string() << " to " << desired_size_bytes;
            RETURN_IF_ERROR(resizePartition(image, desired_size_bytes));
            // It will be recreated by RwDrive.
            base::file::rm(image.concat(".qcow2"));
        }
    }
    return absl::OkStatus();
}

}  // namespace

absl::Status prepareUserDataBaseImage(fs::path init_data, fs::path user_data, uint64_t data_size,
                                      bool wipe_data, bool resize) {
    if (wipe_data) {
        base::file::rm(user_data);
    }

    if (base::file::exists(user_data)) {
        if (!resize) {
            return absl::OkStatus();
        }
        return minimizePartition(user_data, data_size);
    } else {
        if (!base::file::is_dir(init_data)) {
            return absl::InvalidArgumentError(absl::StrCat(
                    "data partition initialization path is not a directory: ", init_data.string()));
        }
        fs::path empty_data_path = init_data / "empty_data_disk";
        if (base::file::exists(empty_data_path)) {
            // Don't create anything - in this case, userdata should be created the same as cache or
            // sdcard.
            return absl::OkStatus();
        }
        return absl::UnimplementedError(
                "no empty_data_disk marker found but data dirs are not supported by this version "
                "of the emulator");
    }
}

}  // namespace android::goldfish
