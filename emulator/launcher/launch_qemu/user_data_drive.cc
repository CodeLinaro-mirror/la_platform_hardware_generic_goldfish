

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

#include "android/fsimage/ext4.h"
#include "android/goldfish/avd.h"
#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "goldfish/file/storage_capacity.h"

namespace android::goldfish {

namespace fs = std::filesystem;

namespace {
std::optional<fs::path> getUserdataScratchDirectory(const Avd& avd) {
    if (avd.Hw().hw_sensor_hinge) {
        return avd.GetContentPath() / "data";
    } else {
        return std::nullopt;
    }
}

absl::StatusOr<fs::path> prepareUserdataSrcDirImpl(fs::path scratch_dir,
                                                   const fs::path& init_data) {
    LOG(INFO) << __func__ << ": init_data=" << init_data << ", scratch_dir=" << scratch_dir;

    const fs::path src_dir = init_data / "misc" / "pixel_9_pro_fold";
    if (!base::file::is_dir(src_dir)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "data partition initialization path is not a directory: ", src_dir.string()));
    }

    RETURN_IF_ERROR(base::file::rm_recursive(scratch_dir));
    const fs::path dst_dir = scratch_dir / "system";
    RETURN_IF_ERROR(base::file::mkdir_recursive(dst_dir, 0755));

    LOG(INFO) << "Copying from " << src_dir << " to " << dst_dir;
    std::error_code ec;
    fs::copy(src_dir, dst_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    if (ec) {
        return absl::InternalError(absl::StrCat("Failed to copy ", src_dir.string(), " to ",
                                                dst_dir.string(), ": ", ec.message()));
    }

    return scratch_dir;
}
}  // namespace

absl::StatusOr<std::optional<fs::path>> prepareUserdataSrcDir(const Avd& avd,
                                                              const fs::path& init_data) {
    if (!base::file::is_dir(init_data)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "data partition initialization path is not a directory: ", init_data.string()));
    }

    std::optional<fs::path> scratch_directory = getUserdataScratchDirectory(avd);
    if (scratch_directory) {
        return prepareUserdataSrcDirImpl(*std::move(scratch_directory), init_data);
    } else if (base::file::exists(init_data / "empty_data_disk")) {
        return std::nullopt;
    }

    LOG(WARNING) << "No empty_data_disk marker found but data dirs are not fully supported by this "
                    "version of the emulator: data partition will be initialized empty";
    return std::nullopt;
}

}  // namespace android::goldfish
