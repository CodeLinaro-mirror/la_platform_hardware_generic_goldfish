

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
#include "android/goldfish/devices/drives/user_data_drive.h"

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "aemu/base/Log.h"
#include "android/base/system/System.h"
#include "android/base/system/storage_capacity.h"
#include "android/emulation/control/adb/adbkey.h"
#include "android/filesystems/ext4_resize.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/config_dirs.h"
#include "android/goldfish/config/emulator.h"
#include "android/utils/path.h"

#include <filesystem>
#include <fstream>

#define D(...) (void)(0)
namespace android::goldfish {

using android::base::System;
using android::goldfish::ConfigDirs;
namespace fs = std::filesystem;

static absl::Status writePublicKey(const fs::path &guestAdbKeyPath,
                                   const std::string &pubKey) {
  std::ofstream pubKeyFile(guestAdbKeyPath);
  if (!pubKeyFile.is_open()) {
    return absl::UnknownError(absl::StrFormat(
        "Error opening public key file: %s", guestAdbKeyPath.string()));
  }
  pubKeyFile << pubKey << std::endl;
  return absl::OkStatus();
}

absl::Status prepareDataFolder(const fs::path &from, const fs::path &to) {
  // The adb_keys file permission will also be set in guest system.
  // Referencing system/core/rootdir/init.usb.rc
  if (fs::exists(to)) {
    dwarning("Erasing existing folder: %s", to.string());
    fs::remove_all(to);
  }

  static const int kAdbKeyDirFilePerm = 02750;
  std::error_code ec;
  fs::copy(from, to, fs::copy_options::recursive, ec);
  if (ec) {
    return absl::DataLossError(
        absl::StrFormat("Failed to copy from: %s to %s due to %s. There might "
                        "be lingering data in %s",
                        from.string(), to.string(), ec.message(), to.string()));
  }
  fs::path adbKeyPubPath = getAdbKeyPath(kPublicKeyFileName);
  fs::path adbKeyPrivPath = getAdbKeyPath(kPrivateKeyFileName);

  if (adbKeyPubPath == "" && adbKeyPrivPath == "") {
    fs::path path = ConfigDirs::getUserDirectory() / kPrivateKeyFileName;
    // try to generate the private key
    if (!adb_auth_keygen(path)) {
      return absl::InternalError(absl::StrFormat(
          "Failed to create a private key in %s", path.string()));
    }
    adbKeyPrivPath = getAdbKeyPath(kPrivateKeyFileName);
    if (adbKeyPrivPath == "") {
      return absl::NotFoundError(absl::StrFormat(
          "Unable discover adb path for: %s", kPrivateKeyFileName));
    }
  }
  fs::path guestAdbKeyDir = to / "misc" / "adb";
  fs::path guestAdbKeyPath = guestAdbKeyDir / "adb_keys";

  path_mkdir_if_needed(guestAdbKeyDir.string().c_str(), kAdbKeyDirFilePerm);
  if (adbKeyPubPath == "") {
    // generate from private key
    std::string pubKey;
    if (pubkey_from_privkey(adbKeyPrivPath, &pubKey)) {
      auto status = writePublicKey(guestAdbKeyPath, pubKey);
      if (!status.ok()) {
        return status;
      }
      D("Using re-constructed public key from %s", adbKeyPrivPath.string());
    }
  } else {
    path_copy_file(guestAdbKeyPath.string().c_str(),
                   adbKeyPubPath.string().c_str());
  }

  // Setting permissions to 0640
  fs::permissions(guestAdbKeyPath,
                  fs::perms::owner_read | fs::perms::owner_write |
                      fs::perms::group_read,
                  fs::perm_options::add);
  return absl::OkStatus();
}

absl::Status UserDataDrive::createImage(const HardwareConfig &hw,
                                        const fs::path data_path) {
  dinfo("Creating image [%s] of size %s", hw.disk_dataPartition_path,
        hw.disk_dataPartition_size.string());
  fs::path empty_data_path = data_path / "empty_data_disk";
  bool shouldUseEmptyDataImg = fs::exists(empty_data_path);
  // &&!(android_foldable_is_pixel_fold());

  absl::Status create_status;
  if (fs::exists(empty_data_path)) {
    create_status = createExt4Image(hw.disk_dataPartition_path,
                                    hw.disk_dataPartition_size, "data");
  } else {
    create_status =
        createExt4ImageFromDirectory(data_path, hw.disk_dataPartition_path,
                                     hw.disk_dataPartition_size, "data");
  }

  // Check if creating user data img succeed
  System::FileSize diskSize;
  if (create_status.ok() &&
      System::get()->pathFileSize(hw.disk_dataPartition_path, &diskSize) &&
      diskSize > 0) {
    return absl::OkStatus();
  }

  fs::remove(hw.disk_dataPartition_path);
  return absl::DataLossError(
      absl::StrFormat("Failed to properly configure the partition. The file "
                      "'%s' has been deleted. Reason: %s",
                      hw.disk_dataPartition_path, create_status.message()));
}

absl::Status UserDataDrive::createUserData(const Emulator &emulator,
                                           const fs::path data_path,
                                           bool asQcow2) {
  auto hw = emulator.avd().hw();
  const Avd &avd = emulator.avd();

  auto initDir = avd.getImageFilePath(Avd::ImageType::INITZIP);
  if (!initDir.ok()) {
    return initDir.status();
  }

  bool needCopyDataPartition = true;
  if (fs::exists(*initDir)) {
    dinfo("Creating ext4 userdata partition: %s from %s", data_path.string(),
          initDir->string());
    auto status = prepareDataFolder(*initDir, data_path);
    if (!status.ok()) {
      derror("Failed to prepare data folder.");
      return status;
    }

    // TODO(jansene):
    // Add support for foldable and display settings.
    //    prepareDisplaySettingXml(hw, data_path);
    // if (feature_is_enabled(kFeature_SupportPixelFold)) {
    //   prepareSkinConfig(hw, data_path);
    // }

    status = createImage(hw, data_path);
    if (!status.ok()) {
      derror("Failed to create user data image %s", data_path.string());
      return status;
    }

    fs::remove_all(data_path);

    // if (asQcow2) {
    auto startTime = std::chrono::steady_clock::now();
    auto qemu_img = System::get()->findBundledExecutable("qemu-img");
    std::string dataimageext4 = std::string(hw.disk_dataPartition_path);
    status = convertImgToQcow2(dataimageext4);
    if (!status.ok()) {
      return status;
    }
    // };
  }

  if (needCopyDataPartition) {
    if (System::get()->pathExists(hw.disk_dataPartition_initPath)) {
      D("Creating: %s by copying from %s ", hw.disk_dataPartition_path,
        hw.disk_dataPartition_initPath);

      if (!fs::copy_file(hw.disk_dataPartition_initPath,
                         hw.disk_dataPartition_path)) {
        return absl::InternalError(
            absl::StrFormat("Could not create %s. Copy operation failed: %s",
                            hw.disk_dataPartition_path, strerror(errno)));
      }

      if (!hw.hw_arc) {
        auto status = resizePartition(hw.disk_dataPartition_path,
                                      hw.disk_dataPartition_size);
        if (!status.ok()) {
          dwarning("Failed to resize partition. Ignoring resize operation. "
                   "Reason: %s",
                   status.message());
        }
      }
    }
  }

  return absl::OkStatus();
}

absl::Status
UserDataDrive::minimizeUserDataPartition(const Emulator &emulator) {
  auto hw = emulator.avd().hw();
  // Check if a resize is needed (current size < configured size)
  // b/196926
  System::FileSize current_data_size;
  if (System::get()->pathIsExt4(hw.disk_dataPartition_path) &&
      System::get()->pathFileSize(hw.disk_dataPartition_path,
                                  &current_data_size)) {
    auto partition_size = hw.disk_dataPartition_size;
    if (hw.disk_dataPartition_size > 0 && current_data_size < partition_size) {
      // Log resize intent
      dwarning("Resizing userdata partition %s from %s to %s",
               hw.disk_dataPartition_path, current_data_size.string(),
               hw.disk_dataPartition_size.string());
      auto status = resizePartition(hw.disk_dataPartition_path,
                                    hw.disk_dataPartition_size);
      if (!status.ok()) {
        auto qcow2 = absl::StrCat(hw.disk_dataPartition_path, ".qcow2");
        dwarning("Partition resize failed. Deleting associated QCOW2 image: %s",
                 qcow2);
        System::get()->deleteFile(qcow2);
      };
    }
  }
  return absl::OkStatus();
}

absl::Status UserDataDrive::initialize(const Emulator &emulator) {
  using android::base::operator""_GiB;
  auto hw = emulator.avd().hw();

  if (exists()) {
    if (!hw.hw_arc) {
      return minimizeUserDataPartition(emulator);
    }
    return absl::OkStatus();
  }

  // auto initImage = emulator.avd().getImagePath(Avd::ImageType::INITDATA);
  // if (!initImage.ok()) {
  //   return initImage.status();
  // }

  if (emulator.avd().playstore()) {
    StorageCapacity kMinPlaystoreImageSize = 6_GiB;
    if (hw.disk_dataPartition_size < kMinPlaystoreImageSize) {
      hw.disk_dataPartition_size = kMinPlaystoreImageSize;
      // TODO(jansene): Now update underlying config.ini file..
    }
  }

  StorageCapacity availableSpace;
  if (!hw.disk_dataPartition_path.empty() &&
      System::get()->pathFreeSpace(hw.disk_dataPartition_path,
                                   &availableSpace)) {
    constexpr double kDataPartitionSafetyFactor = 1.2;
    auto needed = hw.disk_dataPartition_size * kDataPartitionSafetyFactor;

    if (needed > availableSpace) {
      return absl::ResourceExhaustedError(
          absl::StrFormat("Failed to create userdata partition due to "
                          "insufficient disk space. "
                          "Available space at '%s': %s, required space: %s",
                          hw.disk_dataPartition_path, availableSpace.string(),
                          needed.string()));
    }
  }
  if (absl::StartsWith(hw.hw_device_name, "pixel_fold") ||
      absl::StartsWith(hw.hw_device_name, "resizable")) {
    // TODO(jansene): if (!feature_is_enabled(kFeature_SupportPixelFold))
    return absl::AbortedError(
        absl::StrFormat("Device %s requires the foldable feature, but "
                        "the system image does not support it. Please update "
                        "your system image or use a compatible device.",
                        hw.hw_device_name));
  }

  // convert the ext4 to qcow2
  bool bShouldConvertToQcow2 = false;

  // TODO(jansene): Enable features..
  // if (feature_is_enabled(kFeature_DownloadableSnapshot) ||
  //     opts->qcow2_for_userdata || hw.userdata_useQcow2) {
  //     bShouldConvertToQcow2 = true;
  // }

  fs::path data_path = emulator.avd().getContentPath() / "data";
  return createUserData(emulator, data_path, bShouldConvertToQcow2);
}
} // namespace android::goldfish