

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
#include "android/base/system/File.h"
#include "android/base/system/storage_capacity.h"
#include "android/emulation/control/adb/adbkey.h"
#include "android/filesystems/ext4_resize.h"
#include "android/filesystems/ext4_utils.h"
#include "android/goldfish/config/config_dirs.h"

namespace android::goldfish {

namespace fs = std::filesystem;

namespace {

using android::base::StorageCapacity;
using android::base::operator""_MiB;
using android::base::operator""_TiB;

using ::goldfish::adb::adb_auth_keygen;
using ::goldfish::adb::getAdbKeyPath;

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
            fs::remove(fs::path(image).concat(".qcow2"));
        }
    }
    return absl::OkStatus();
}

absl::Status createExt4ImageFromDirectory(fs::path source, fs::path destination,
                                          StorageCapacity size, std::string mount_point) {
    if (android_createExt4ImageFromDir(destination.string().c_str(), source.string().c_str(),
                                       size.bytes(), mount_point.c_str()) == 0) {
        return absl::OkStatus();
    }

    return absl::InternalError(
            absl::StrFormat("Failed to create Ext4 image from directory '%s' to '%s'",
                            source.string(), destination.string()));
}

absl::Status writePublicKey(const fs::path& guestAdbKeyPath, const std::string& pubKey) {
    std::ofstream pubKeyFile(guestAdbKeyPath);
    if (!pubKeyFile.is_open()) {
        return absl::UnknownError(
                absl::StrFormat("Error opening public key file: %s", guestAdbKeyPath.string()));
    }
    pubKeyFile << pubKey << std::endl;
    return absl::OkStatus();
}

absl::Status prepareDataFolder(const fs::path& from, const fs::path& to) {
    // The adb_keys file permission will also be set in guest system.
    // Referencing system/core/rootdir/init.usb.rc
    static const int kAdbKeyDirFilePerm = 02750;
    std::error_code ec;
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) {
        return absl::DataLossError(
                absl::StrFormat("Failed to copy from: %s to %s due to %s. There might "
                                "be lingering data in %s",
                                from.string(), to.string(), ec.message(), to.string()));
    }
    fs::path adbKeyPubPath = getAdbKeyPath(::goldfish::adb::kPublicKeyFileName);
    fs::path adbKeyPrivPath = getAdbKeyPath(::goldfish::adb::kPrivateKeyFileName);

    if (adbKeyPubPath == "" && adbKeyPrivPath == "") {
        fs::path path = ConfigDirs::getUserDirectory() / ::goldfish::adb::kPrivateKeyFileName;
        // try to generate the private key
        if (!adb_auth_keygen(path)) {
            return absl::InternalError(
                    absl::StrFormat("Failed to create a private key in %s", path.string()));
        }
        adbKeyPrivPath = getAdbKeyPath(::goldfish::adb::kPrivateKeyFileName);
        if (adbKeyPrivPath == "") {
            return absl::NotFoundError(absl::StrFormat("Unable discover adb path for: %s",
                                                       ::goldfish::adb::kPrivateKeyFileName));
        }
    }
    fs::path guestAdbKeyDir = to / "misc" / "adb";
    fs::path guestAdbKeyPath = guestAdbKeyDir / "adb_keys";

    fs::create_directories(guestAdbKeyDir);
    if (adbKeyPubPath == "") {
        // generate from private key
        std::string pubKey;
        if (::goldfish::adb::pubkey_from_privkey(adbKeyPrivPath, &pubKey)) {
            auto status = writePublicKey(guestAdbKeyPath, pubKey);
            if (!status.ok()) {
                return status;
            }
            VLOG(1) << "Using re-constructed public key from " << adbKeyPrivPath.string();
        }
    } else {
        std::error_code ec;
        fs::copy(adbKeyPubPath, guestAdbKeyPath, ec);
        if (ec) {
            return absl::DataLossError(absl::StrFormat("Failed to copy from: %s to %s due to %s",
                                                       adbKeyPubPath.string(),
                                                       guestAdbKeyPath.string(), ec.message()));
        }
    }

    // Setting permissions to 0640
    fs::permissions(guestAdbKeyPath,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read,
                    fs::perm_options::add);
    return absl::OkStatus();
}

}  // namespace

absl::Status prepareUserDataBaseImage(fs::path init_data, fs::path user_data, uint64_t data_size,
                                      bool wipe_data, bool resize) {
    if (wipe_data) {
        fs::remove(user_data);
    }

    if (fs::exists(user_data)) {
        if (!resize) {
            return absl::OkStatus();
        }
        return minimizePartition(user_data, data_size);
    } else {
        if (!fs::is_directory(init_data)) {
            return absl::InvalidArgumentError(absl::StrCat(
                    "data partition initialization path is not a directory: ", init_data.string()));
        }
        fs::path empty_data_path = init_data / "empty_data_disk";
        if (fs::exists(empty_data_path)) {
            // Don't create anything - in this case, userdata should be created the same as cache or
            // sdcard.
            return absl::OkStatus();
        }

        // TODO just a tmpdir
        fs::path tmp_data_path = user_data.parent_path() / "data";
        VLOG(1) << "Creating ext4 userdata partition: " << tmp_data_path << " from " << init_data;
        RETURN_IF_ERROR(prepareDataFolder(init_data, tmp_data_path));

        LOG(INFO) << "Creating image [" << user_data << "] of size " << data_size;
        absl::Status create_status =
                createExt4ImageFromDirectory(tmp_data_path, user_data, data_size, "userdata");
        fs::remove_all(tmp_data_path);
        RETURN_IF_ERROR(create_status);

        // Check if creating img succeed
        ASSIGN_OR_RETURN(auto diskSize, base::file::file_size(user_data));
        if (diskSize > 0) {
            return absl::OkStatus();
        } else {
            fs::remove(user_data);
            return absl::DataLossError(
                    absl::StrFormat("Failed to properly configure the partition. The file "
                                    "'%s' has been deleted. Reason: %s",
                                    user_data, create_status.message()));
        }
    }

    return absl::OkStatus();
}

}  // namespace android::goldfish
