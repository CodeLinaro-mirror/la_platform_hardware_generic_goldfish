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
#include <memory>

#include "absl/log/log.h"

#include "make_ext4fs.h"

#define DEBUG_EXT4 0

#define EXT4_LOG LOG_IF(INFO, DEBUG_EXT4)
#define EXT4_PLOG PLOG_IF(INFO, DEBUG_EXT4)
#define EXT4_ERROR LOG_IF(ERROR, DEBUG_EXT4)
#define EXT4_PERROR PLOG_IF(ERROR, DEBUG_EXT4)

auto android_createEmptyExt4Image(const char* filePath, uint64_t size, const char* mountpoint)
        -> int {
    return android_createExt4ImageFromDir(filePath, nullptr, size, mountpoint);
}

auto android_createExt4ImageFromDir(const char* dstFilePath, const char* srcDirectory,
                                    uint64_t size, const char* mountpoint) -> int {
    int ret = ::make_ext4fs_from_dir(dstFilePath, srcDirectory, size, mountpoint, nullptr, -1);
    if (ret < 0) {
        EXT4_ERROR << "Failed to create ext4 image at: " << dstFilePath;
    }
    return ret;
}