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

#include "android/goldfish/hardware_config.h"

#include <filesystem>

#include "android/goldfish/ini_file.h"
#include "goldfish/file/storage_capacity.h"

namespace android::goldfish {

HardwareConfig::HardwareConfig() {
#define HWCFG_BOOL(n, s, d, a, t) n = 0;
#define HWCFG_INT(n, s, d, a, t) n = 0;
#define HWCFG_STRING(n, s, d, a, t) n.clear();
#define HWCFG_DOUBLE(n, s, d, a, t) n = 0.0;
#define HWCFG_DISKSIZE(n, s, d, a, t) n = 0;

#include "avd/hw-config-defs.h"
}

void HardwareConfig::Load(const IniFile& ini) {
    /* use the magic of macros to implement the hardware configuration loaded */
#define HWCFG_BOOL(n, s, d, a, t) (n) = ini.GetBool(s, d);
#define HWCFG_INT(n, s, d, a, t) (n) = ini.GetInt(s, d);
#define HWCFG_STRING(n, s, d, a, t) (n) = ini.GetString(s, d);
#define HWCFG_DOUBLE(n, s, d, a, t) (n) = ini.GetDouble(s, d);
#define HWCFG_DISKSIZE(n, s, d, a, t) (n) = ini.GetDiskSize(s, d);

#include "avd/hw-config-defs.h"

    hw_sdCard = ini.GetBool("hw.sdCard", false);
    hw_sd_card_size = ini.GetDiskSize("sdcard.size", hw_sd_card_size.Bytes());
}

void HardwareConfig::ApplyDefaults(const fs::path& sdk_root_path, const fs::path& avd_home_path) {
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

void HardwareConfig::Write(IniFile* ini) const {
#define HWCFG_BOOL(n, s, d, a, t) ini->SetBool(s, n);
#define HWCFG_INT(n, s, d, a, t) ini->SetInt(s, n);
#define HWCFG_STRING(n, s, d, a, t) ini->SetString(s, n);
#define HWCFG_DOUBLE(n, s, d, a, t) ini->SetDouble(s, n);
#define HWCFG_DISKSIZE(n, s, d, a, t) ini->SetDiskSize(s, n);

#include "avd/hw-config-defs.h"

    ini->SetDiskSize("sdcard.size", hw_sd_card_size.Bytes());
}

}  // namespace android::goldfish
