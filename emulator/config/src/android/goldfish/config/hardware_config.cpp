// Copyright 2024 The Android Open Source Project
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
#include "android/goldfish/config/hardware_config.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "aemu/base/files/IniFile.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/config_dirs.h"
#include <android/base/system/storage_capacity.h>

namespace android::goldfish {

HardwareConfig::HardwareConfig() {
#define HWCFG_BOOL(n, s, d, a, t) n = 0;
#define HWCFG_INT(n, s, d, a, t) n = 0;
#define HWCFG_STRING(n, s, d, a, t) n.clear();
#define HWCFG_DOUBLE(n, s, d, a, t) n = 0.0;
#define HWCFG_DISKSIZE(n, s, d, a, t) n = 0;

#include "host-common/hw-config-defs.h"
}

void HardwareConfig::load(Avd *avd, IniFile *ini) {
  /* use the magic of macros to implement the hardware configuration loaded */
#define HWCFG_BOOL(n, s, d, a, t) (n) = ini->getBool(s, d);
#define HWCFG_INT(n, s, d, a, t) (n) = ini->getInt(s, d);
#define HWCFG_STRING(n, s, d, a, t) (n) = ini->getString(s, d);
#define HWCFG_DOUBLE(n, s, d, a, t) (n) = ini->getDouble(s, d);
#define HWCFG_DISKSIZE(n, s, d, a, t) (n) = ini->getDiskSize(s, d);

#include "host-common/hw-config-defs.h"

  hw_sdCard = ini->getDiskSize("sdcard.size", 0) > 0;
  if (android_sdk_root.empty()) {
    android_sdk_root = ConfigDirs::getSdkRootDirectory().string();
  }
  if (android_avd_home.empty()) {
    android_avd_home = ConfigDirs::getAvdRootDirectory().string();
  }

  auto img = avd->getImageFilePath(Avd::ImageType::ENCRYPTIONKEY);
  if (disk_encryptionKeyPartition_path.empty() && img.ok()) {
    disk_encryptionKeyPartition_path = img->string();
  }

  /* Bug: 307296354
     when config.ini does not specify orientation, set it to natural
     orientation: for phone, it is still portrait, for tablet landscape
  */
  if (hw_initialOrientation == "natural") {
    if (hw_lcd_width > hw_lcd_height) {
      hw_initialOrientation = "landscape";
    } else {
      hw_initialOrientation = "portrait";
    }
  }

  // TODO(jansene): Setup command line overrides..
  disk_ramdisk_path = avd->getImageFilePath(Avd::ImageType::USERRAMDISK)
                          .value_or(fs::path())
                          .string();
  if (disk_ramdisk_path.empty()) {
    disk_ramdisk_path = avd->getImageFilePath(Avd::ImageType::RAMDISK)
                            .value_or(fs::path())
                            .string();
  }

  disk_dataPartition_path = avd->getImageFilePath(Avd::ImageType::USERDATA)
                                .value_or(fs::path())
                                .string();

  if (disk_dataPartition_path.empty()) {
    disk_dataPartition_path =
        (avd->getContentPath() /
         avd->getImageFilenameByType(Avd::ImageType::USERDATA))
            .string();
  }

  disk_systemPartition_initPath =
      avd->getSystemImagePath(Avd::ImageType::USERSYSTEM)
          .value_or(fs::path())
          .string();

  if (disk_systemPartition_initPath.empty() ||
      !fs::exists(disk_systemPartition_initPath)) {
    disk_systemPartition_initPath =
        avd->getSystemImagePath(Avd::ImageType::INITSYSTEM)
            .value_or(fs::path())
            .string();
  }

  disk_encryptionKeyPartition_path =
      avd->getImageFilePath(Avd::ImageType::ENCRYPTIONKEY)
          .value_or(fs::path())
          .string();

  if (disk_encryptionKeyPartition_path.empty() &&
      !disk_dataPartition_path.empty()) {
    disk_encryptionKeyPartition_path =
        (fs::path(disk_dataPartition_path).parent_path() / "encryptionkey.img")
            .string();
  }

  hw_sdCard_path = (avd->getContentPath() /
                    avd->getImageFilenameByType(Avd::ImageType::SDCARD))
                       .string();

  disk_cachePartition_size =
      StorageCapacity(66, StorageCapacity::Unit::MiB).bytes();
  disk_cachePartition_path = avd->getImageFilePath(Avd::ImageType::CACHE)
                                 .value_or(fs::path())
                                 .string();
  if (disk_cachePartition_path.empty()) {
    disk_cachePartition_path =
        (avd->getContentPath() /
         avd->getImageFilenameByType(Avd::ImageType::CACHE))
            .string();
  }

  hw_sdCard_size = ini->getDiskSize("sdcard.size", hw_sdCard_size.bytes());
}

} // namespace android::goldfish