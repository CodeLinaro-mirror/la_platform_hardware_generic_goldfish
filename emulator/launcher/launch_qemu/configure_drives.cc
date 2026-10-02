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

#include "configure_drives.h"

#include <algorithm>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/status/status_macros.h"
#include "user_data_drive.h"

namespace android::goldfish::internal {

namespace {
absl::StatusOr<fs::path> getOverridePath(const char* path_str) {
    fs::path path(path_str);
    if (base::file::exists(path)) {
        return path;
    } else {
        return absl::NotFoundError(absl::StrCat("File not found: ", path_str));
    }
}

fs::path getWritableImgPath(const Avd& avd, Avd::ImageType user_image_type) {
    return avd.GetContentPath() / Avd::GetImageFilename(user_image_type);
}

absl::StatusOr<fs::path> getWritableImgPath(const Avd& avd, Avd::ImageType user_image_type,
                                            const char* override_path) {
    if (override_path) {
        return getOverridePath(override_path);
    } else {
        return getWritableImgPath(avd, user_image_type);
    }
}

absl::StatusOr<fs::path> getUserdataImgPath(const Avd& avd, const char* override_path) {
    if (override_path) {
        return getOverridePath(override_path);
    } else {
        fs::path path = getWritableImgPath(avd, Avd::ImageType::USERDATA_OLD);
        if (!base::file::exists(path)) {
            path = getWritableImgPath(avd, Avd::ImageType::USERDATA);
        }
        return path;
    }
}

fs::path to_qcow2(fs::path base) {
    return base.concat(".qcow2");
}

fs::path getQcow2Path(const Avd& avd, Avd::ImageType user_image_type) {
    return to_qcow2(getWritableImgPath(avd, user_image_type));
}

uint64_t getDataSize(const Avd& avd, const AndroidOptions& opts) {
    // studio avd manager does not allow user to change partition size, set a
    // lower limit to 6GB.
    constexpr uint64_t kMinPlaystoreImageSize = 6ULL * 1024 * 1024 * 1024;
    uint64_t data_size = avd.Hw().disk_dataPartition_size.Bytes();
    if (opts.partition_size != nullptr) {
        uint64_t size_mib;
        if (absl::SimpleAtoi(opts.partition_size, &size_mib)) {
            data_size = size_mib * 1024 * 1024;
        } else {
            LOG(ERROR) << "Failed to parse -partition-size flag: " << opts.partition_size;
        }
    }
    return std::max(data_size, kMinPlaystoreImageSize);
}

uint64_t getSdcardSize(const Avd& avd, const AndroidOptions& opts) {
    // TODO minimum size?
    return avd.Hw().hw_sd_card_size.Bytes();
}

std::unique_ptr<PciDevice> MakeDrive(std::string id, std::string pci_address, const bool rw,
                                     fs::path backing_image, fs::path qcow_image,
                                     const uint64_t backing_image_create_size,
                                     std::optional<fs::path> backing_image_create_src_dir) {
    if (rw) {
        if (backing_image_create_size) {
            if (backing_image_create_src_dir) {
                return std::make_unique<WritableDirectoryDrive>(
                        std::move(id), std::move(pci_address), std::move(backing_image),
                        std::move(qcow_image), *std::move(backing_image_create_src_dir),
                        backing_image_create_size);
            } else {
                return std::make_unique<BlankWritableDrive>(
                        std::move(id), std::move(pci_address), std::move(backing_image),
                        std::move(qcow_image), backing_image_create_size);
            }
        } else {
            return std::make_unique<WritableImgDrive>(std::move(id), std::move(pci_address),
                                                      std::move(backing_image),
                                                      std::move(qcow_image));
        }
    } else {
        return std::make_unique<RoDrive>(std::move(id), std::move(pci_address),
                                         std::move(backing_image));
    }
}
}  // namespace

absl::StatusOr<std::vector<std::unique_ptr<PciDevice>>> getDiskConfigs(const Avd& avd,
                                                                       const AndroidOptions& opts) {
    const auto& sys_img_paths = avd.GetSystemImagePaths();

    ASSIGN_OR_RETURN(const auto userdata_path, getUserdataImgPath(avd, opts.data));
    uint64_t userdata_create_size = 0;
    std::optional<fs::path> userdata_src_dir;
    if (!base::file::exists(userdata_path)) {
        ASSIGN_OR_RETURN(userdata_src_dir, prepareUserdataSrcDir(avd, sys_img_paths.data_dir));
        userdata_create_size = getDataSize(avd, opts);
    }

    const bool rw_sys = opts.writable_system;
    if (rw_sys) {
        LOG(WARNING) << "System image is writable";
    }

    std::vector<std::unique_ptr<PciDevice>> drives;
    const auto add_drive = [&drives](std::string id, std::string pci_address, bool rw,
                                     fs::path image_path, fs::path qcow_path,
                                     uint64_t image_create_size = 0,
                                     std::optional<fs::path> image_create_src_dir = std::nullopt) {
        drives.push_back(MakeDrive(std::move(id), std::move(pci_address), rw, std::move(image_path),
                                   std::move(qcow_path), image_create_size,
                                   std::move(image_create_src_dir)));
    };

    const auto add_null_drive = [&drives](std::string id, std::string pci_address) {
        drives.push_back(std::make_unique<NullDrive>(std::move(id), std::move(pci_address)));
    };

    // Currently this must be the first drive on ARM to match the androidboot.boot_devices
    // parameter set in initrd_device.cpp.
    add_drive("system", "03.0", /*rw=*/rw_sys, sys_img_paths.system_image,
              getQcow2Path(avd, Avd::ImageType::SYSTEM));
    // Encryption must be second for ARM - to have path
    // "/dev/block/platform/a003c00.virtio_mmio/by-name/metadata".
    add_drive("encrypt", "06.0", /*rw=*/true, sys_img_paths.encryption_key_image,
              getQcow2Path(avd, Avd::ImageType::ENCRYPTIONKEY));
    add_drive("userdata", "05.0", /*rw=*/true, userdata_path, to_qcow2(userdata_path),
              userdata_create_size, std::move(userdata_src_dir));
    add_drive("vendor", "07.0", /*rw=*/rw_sys, sys_img_paths.vendor_image,
              getQcow2Path(avd, Avd::ImageType::VENDOR));
    // Placeholder drive to preserve /dev/block/vd* device ordering expected by fstab.ranchu.
    add_null_drive("cache", "04.0");

#ifdef __x86_64__
    if (avd.Hw().hw_sdCard) {
        ASSIGN_OR_RETURN(const auto sdcard_path,
                         getWritableImgPath(avd, Avd::ImageType::SDCARD, opts.sdcard));
        add_drive("sdcard", "08.0", true, sdcard_path, to_qcow2(sdcard_path),
                  getSdcardSize(avd, opts));
    }
#endif

    return drives;
}

}  // namespace android::goldfish::internal
