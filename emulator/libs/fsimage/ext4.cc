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

#include "goldfish/fsimage/ext4.h"

#include "absl/strings/str_format.h"

#include "make_ext4fs.h"

namespace goldfish::fsimage {

namespace {

using android::base::StorageCapacity;

absl::Status CreateExt4ImageFromDirImpl(const fs::path& image, const fs::path* src_dir,
                                        const StorageCapacity size,
                                        const std::string& mount_point) {
    const int ret = ::make_ext4fs_from_dir(image.string().c_str(),
                                           src_dir ? src_dir->string().c_str() : nullptr,
                                           size.Bytes(), mount_point.c_str(), -1);
    if (ret) {
        if (src_dir) {
            return absl::InternalError(absl::StrFormat(
                    "Failed to create an Ext4 image from '%s' in '%s' of size %d bytes, ret=%d",
                    src_dir->string(), image.string(), size.Bytes(), ret));
        } else {
            return absl::InternalError(absl::StrFormat(
                    "Failed to create an empty Ext4 image '%s' of size %d bytes, ret=%d",
                    image.string(), size.Bytes(), ret));
        }
    }

    return absl::OkStatus();
}

}  // namespace

absl::Status CreateEmptyExt4Image(const fs::path& image, const StorageCapacity size,
                                  const std::string& mount_point) {
    return CreateExt4ImageFromDirImpl(image, nullptr, size, mount_point);
}

absl::Status CreateExt4ImageFromDir(const fs::path& image, const fs::path& src_dir,
                                    const StorageCapacity size, const std::string& mount_point) {
    return CreateExt4ImageFromDirImpl(image, &src_dir, size, mount_point);
}

}  // namespace goldfish::fsimage
