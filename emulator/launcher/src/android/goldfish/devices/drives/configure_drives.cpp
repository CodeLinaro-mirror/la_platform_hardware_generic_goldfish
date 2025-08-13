

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
#include <filesystem>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "aemu/base/utils/status_macros.h"

#include "user_data_drive.h"

namespace android::goldfish::internal {

namespace {
DiskConfig diskConfig(const Avd &avd, std::string_view id, std::string_view pci_address, bool is_writable, std::optional<fs::path> system_image, fs::path user_image, uint64_t size_bytes, bool wipe_existing = false) {
    return {
        .id = std::string(id),
        .pci_address = std::string(pci_address),
        .is_writable = is_writable,
        .system_image_path_ro = system_image,
        .user_image_path = user_image,
        .size_bytes = size_bytes,
        .wipe_existing = wipe_existing,
    };
}

absl::StatusOr<fs::path> getSystemImage(const Avd &avd, Avd::ImageType sys_image_type, char *flag_override) {
    fs::path p;
    if (flag_override != nullptr) {
        p = fs::path(flag_override);
        if (!fs::exists(p)) {
            return absl::NotFoundError(absl::StrCat("System image specified by flag override not found: ", flag_override));
        }
    } else {
        ASSIGN_OR_RETURN(p, avd.getSystemImageFilePath(sys_image_type));
    }
    return p;
}

fs::path getUserImage(const Avd &avd, Avd::ImageType user_image_type, char *flag_override) {
    if (flag_override != nullptr) {
        return fs::path(flag_override);
    }
    return avd.getContentPath() / Avd::getImageFilename(user_image_type);
}

uint64_t getDataSize(const Avd &avd, const AndroidOptions &opts) {
    // studio avd manager does not allow user to change partition size, set a
    // lower limit to 6GB.
    constexpr uint64_t kMinPlaystoreImageSize = 6ULL * 1024 * 1024 * 1024;
    uint64_t data_size = avd.hw().disk_dataPartition_size.bytes();
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

uint64_t getCacheSize(const Avd &avd, const AndroidOptions &opts) {
    constexpr uint64_t kMinCacheSize = 66ULL * 1024 * 1024;
    uint64_t cache_size = avd.hw().disk_cachePartition_size.bytes();
    if (opts.cache_size != nullptr) {
        uint64_t size_mib;
        if (absl::SimpleAtoi(opts.cache_size, &size_mib)) {
            cache_size = size_mib * 1024 * 1024;
        } else {
            LOG(ERROR) << "Failed to parse -cache-size flag: " << opts.cache_size;
        }
    }
    return std::max(cache_size, kMinCacheSize);
}

uint64_t getSdcardSize(const Avd &avd, const AndroidOptions &opts) {
    // TODO minimum size?
    return avd.hw().hw_sdCard_size.bytes();
}
} // namespace

absl::StatusOr<std::vector<DiskConfig>> getDiskConfigs(const Avd &avd, const AndroidOptions &opts) {
    ASSIGN_OR_RETURN(fs::path system, getSystemImage(avd, Avd::ImageType::INITSYSTEM, opts.system));
    ASSIGN_OR_RETURN(fs::path encrypt, getSystemImage(avd, Avd::ImageType::ENCRYPTIONKEY, opts.encryption_key));
    ASSIGN_OR_RETURN(fs::path vendor, getSystemImage(avd, Avd::ImageType::INITVENDOR, opts.vendor));

    ASSIGN_OR_RETURN(fs::path init_data, getSystemImage(avd, Avd::ImageType::INITZIP, nullptr));
    // TODO check for system INITDATA file if INITZIP dir doesn't exist?

    auto user_system = getUserImage(avd, Avd::ImageType::USERSYSTEM, nullptr);
    auto user_encrypt = getUserImage(avd, Avd::ImageType::ENCRYPTIONKEY, nullptr);
    auto user_vendor = getUserImage(avd, Avd::ImageType::USERVENDOR, nullptr);
    auto user_data = getUserImage(avd, Avd::ImageType::USERDATA, opts.data);
    auto user_cache = getUserImage(avd, Avd::ImageType::CACHE, opts.cache);
    auto user_sdcard = getUserImage(avd, Avd::ImageType::SDCARD, opts.sdcard);

    uint64_t data_size = getDataSize(avd, opts);
    uint64_t cache_size = getCacheSize(avd, opts);
    uint64_t sdcard_size = getSdcardSize(avd, opts);

    bool rw_sys = opts.writable_system;
    bool wipe_data = opts.wipe_data;

    if (rw_sys) {
        LOG(WARNING) << "System image is writable";
    }

    // Data partition can have special case initialisation. If it can be created normally then this function won't create it. This function might remove the qcow2 file so that it can be recreated.
    RETURN_IF_ERROR(prepareUserDataBaseImage(init_data, user_data, data_size, wipe_data, !avd.hw().hw_arc));

    return std::vector<DiskConfig>{
        // Currently this must be the first drive on ARM to match the androidboot.boot_devices parameter
        // set in initrd_device.cpp.
        diskConfig(avd, "system", "03.0", rw_sys, system, user_system, 0),
        // Encryption must be second for ARM - to have path
        // "/dev/block/platform/a003c00.virtio_mmio/by-name/metadata".
        diskConfig(avd, "encrypt", "06.0", true, encrypt, user_encrypt, 0, wipe_data),
        diskConfig(avd, "vendor", "07.0", rw_sys, vendor, user_vendor, 0),
        diskConfig(avd, "userdata", "05.0", true, std::nullopt, user_data, data_size),
        diskConfig(avd, "cache", "04.0", true, std::nullopt, user_cache, cache_size, wipe_data),
        diskConfig(avd, "sdcard", "08.0", true, std::nullopt, user_sdcard, sdcard_size, wipe_data),
    };
}

}  // namespace android::goldfish::internal