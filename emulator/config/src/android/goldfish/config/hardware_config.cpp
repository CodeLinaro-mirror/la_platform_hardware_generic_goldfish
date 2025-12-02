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

#include <filesystem>

#include "android/base/system/storage_capacity.h"
#include "android/files/IniFile.h"

namespace android::goldfish {

HardwareConfig::HardwareConfig() {
#define HWCFG_BOOL(n, s, d, a, t) n = 0;
#define HWCFG_INT(n, s, d, a, t) n = 0;
#define HWCFG_STRING(n, s, d, a, t) n.clear();
#define HWCFG_DOUBLE(n, s, d, a, t) n = 0.0;
#define HWCFG_DISKSIZE(n, s, d, a, t) n = 0;

#include "avd/hw-config-defs.h"
}

void HardwareConfig::load(const IniFile& ini) {
    /* use the magic of macros to implement the hardware configuration loaded */
#define HWCFG_BOOL(n, s, d, a, t) (n) = ini.getBool(s, d);
#define HWCFG_INT(n, s, d, a, t) (n) = ini.getInt(s, d);
#define HWCFG_STRING(n, s, d, a, t) (n) = ini.getString(s, d);
#define HWCFG_DOUBLE(n, s, d, a, t) (n) = ini.getDouble(s, d);
#define HWCFG_DISKSIZE(n, s, d, a, t) (n) = ini.getDiskSize(s, d);

#include "avd/hw-config-defs.h"

    hw_sdCard = ini.getDiskSize("sdcard.size", 0) > 0;
    hw_sdCard_size = ini.getDiskSize("sdcard.size", hw_sdCard_size.bytes());
}

void HardwareConfig::applyDefaults(const fs::path& sdk_root_path, const fs::path& avd_home_path) {
    if (android_sdk_root.empty()) {
        android_sdk_root = sdk_root_path.string();
    }
    if (android_avd_home.empty()) {
        android_avd_home = avd_home_path.string();
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

    // If minigbm (always the case for this version of the emulator).
    hw_gltransport = "virtio-gpu-pipe";
}

void HardwareConfig::write(IniFile* ini) {
#define HWCFG_BOOL(n, s, d, a, t) ini->setBool(s, n);
#define HWCFG_INT(n, s, d, a, t) ini->setInt(s, n);
#define HWCFG_STRING(n, s, d, a, t) ini->setString(s, n);
#define HWCFG_DOUBLE(n, s, d, a, t) ini->setDouble(s, n);
#define HWCFG_DISKSIZE(n, s, d, a, t) ini->setDiskSize(s, static_cast<IniFile::DiskSize>(n));

#include "avd/hw-config-defs.h"

    ini->setDiskSize("sdcard.size", hw_sdCard_size.bytes());
}

}  // namespace android::goldfish
