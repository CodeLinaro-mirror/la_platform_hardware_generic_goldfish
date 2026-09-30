

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

#include "android/filesystems/ext4_resize.h"
#include "android/filesystems/ext4_utils.h"
#include "android/goldfish/avd.h"
#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "goldfish/file/storage_capacity.h"

namespace android::goldfish {

namespace fs = std::filesystem;

std::optional<fs::path> getUserSrcDirectory(const Avd& avd) {
    if (avd.Hw().hw_sensor_hinge) {
        return avd.GetContentPath() / "data";
    } else {
        return std::nullopt;
    }
}

absl::Status prepareUserDataBaseImage(const Avd& avd, const fs::path& init_data,
                                      const fs::path& user_data) {
    if (base::file::exists(user_data)) {
        return absl::OkStatus();
    }

    if (!base::file::is_dir(init_data)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "data partition initialization path is not a directory: ", init_data.string()));
    }

    std::optional<fs::path> src_directory = getUserSrcDirectory(avd);
    if (src_directory) {
        LOG(INFO) << "prepareUserDataBaseImage: init_data=" << init_data
                  << ", user_data=" << user_data << ", src_directory=" << src_directory->string();

        LOG(INFO) << "wiping and recreating " << *src_directory;
        RETURN_IF_ERROR(base::file::rm_recursive(*src_directory));
        fs::path dest_dir = *src_directory / "system";
        RETURN_IF_ERROR(base::file::mkdir_recursive(dest_dir, 0755));
        fs::path src_init = init_data / "misc" / "pixel_9_pro_fold";
        if (base::file::is_dir(src_init)) {
            LOG(INFO) << "Copying from " << src_init << " to " << dest_dir;
            std::error_code ec;
            fs::copy(src_init, dest_dir,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                return absl::InternalError(absl::StrCat("Failed to copy ", src_init.string(),
                                                        " to ", dest_dir.string(), ": ",
                                                        ec.message()));
            }
        } else {
            return absl::InvalidArgumentError(absl::StrCat(
                    "data partition initialization path is not a directory: ", init_data.string()));
        }

        return absl::OkStatus();
    } else if (base::file::exists(init_data / "empty_data_disk")) {
        // Don't create anything - in this case, userdata should be created the same as cache or
        // sdcard.
        return absl::OkStatus();
    }

    LOG(WARNING) << "No empty_data_disk marker found but data dirs are not fully supported by this "
                    "version of the emulator: data partition will be initialized empty";
    return absl::OkStatus();
}

}  // namespace android::goldfish
