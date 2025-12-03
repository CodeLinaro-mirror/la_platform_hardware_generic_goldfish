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

#include "android/filesystems/ext4_utils.h"

#include <cstdint>
#include <cstring>

#include "absl/log/log.h"

#include "make_ext4fs.h"

namespace android::filesystems {

namespace fs = std::filesystem;

int android_createEmptyExt4Image(fs::path dstFilePath, uint64_t size, const char* mountpoint) {
    int ret = ::make_ext4fs_from_dir(dstFilePath.string().c_str(), nullptr, size, mountpoint, -1);
    if (ret < 0) {
        LOG(ERROR) << "Failed to create ext4 image at: " << dstFilePath;
    }
    return ret;
}

}  // namespace android::filesystems
