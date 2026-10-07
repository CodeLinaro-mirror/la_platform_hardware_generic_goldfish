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

#include "android/fsimage/ext4.h"

#include "absl/log/log.h"

#include "make_ext4fs.h"

namespace android::fsimage {

int CreateEmptyExt4Image(const fs::path& image, uint64_t size, const char* mountpoint) {
    int ret = ::make_ext4fs_from_dir(image.string().c_str(), nullptr, size, mountpoint, -1);
    if (ret < 0) {
        LOG(ERROR) << "Failed to create ext4 image at: " << image;
    }
    return ret;
}

int CreateExt4ImageFromDir(const fs::path& image, const fs::path& src_dir, uint64_t size,
                           const char* mountpoint) {
    int ret = ::make_ext4fs_from_dir(image.string().c_str(), src_dir.string().c_str(), size,
                                     mountpoint, -1);
    if (ret < 0) {
        LOG(ERROR) << "Failed to create ext4 image at: " << image << " from src " << src_dir;
    }
    return ret;
}

}  // namespace android::fsimage
