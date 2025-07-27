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
#include "android/goldfish/config/avd.h"

#include "absl/log/log.h"

#include <android/goldfish/config/hardware_config.h>

#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "aemu/base/files/IniFile.h"
#include "android/base/system/System.h"
#include "android/goldfish/config/config_dirs.h"
#include "android/goldfish/config/keys.h"
#include "host-common/constants.h"
#include "host-common/hw-config.h"

/* technical note on how all of this is supposed to work:
 *
 * Each AVD corresponds to a "content directory" that is used to
 * store persistent disk images and configuration files. Most remarkable
 * are:
 *
 * - a "config.ini" file used to hold configuration information for the
 *   AVD
 *
 * - mandatory user data image ("userdata-qemu.img") and cache image
 *   ("cache.img")
 *
 * - optional mutable system image ("system-qemu.img"), kernel image
 *   ("kernel-qemu") and read-only ramdisk ("ramdisk.img")
 *
 * When starting up an AVD, the emulator looks for relevant disk images
 * in the content directory. If it doesn't find a given image there, it
 * will try to search in the list of system directories listed in the
 * 'config.ini' file through one of the following (key,value) pairs:
 *
 *    images.sysdir.1 = <first search path>
 *    images.sysdir.2 = <second search path>
 *
 * The search paths can be absolute, or relative to the root SDK installation
 * path (which is determined from the emulator program's location, or from the
 * ANDROID_SDK_ROOT environment variable).
 *
 * Individual image disk search patch can be over-riden on the command-line
 * with one of the usual options.
 */
namespace android::goldfish {

using android::base::System;
using PropertyList = const std::array<std::string, 3>;
using DeviceType = Avd::DeviceType;

namespace {

const std::string_view _imageFileNames[static_cast<int>(Avd::ImageType::AVD_IMAGE_MAX)] = {
#define _AVD_IMG(x, y, z) y,
        AVD_IMAGE_LIST
#undef _AVD_IMG
};

struct ApiLevelInfo {
    std::string_view dessertName;
    std::string_view fullName;
};

const absl::flat_hash_map<int, ApiLevelInfo> kApiLevelInfo = {
        {10, {"Gingerbread", "2.3.3 (Gingerbread) - API 10 (Rev 2)"}},
        {14, {"Ice Cream Sandwich", "4.0 (Ice Cream Sandwich) - API 14 (Rev 4)"}},
        {15, {"Ice Cream Sandwich", "4.0.3 (Ice Cream Sandwich) - API 15 (Rev 5)"}},
        {16, {"Jelly Bean", "4.1 (Jelly Bean) - API 16 (Rev 5)"}},
        {17, {"Jelly Bean", "4.2 (Jelly Bean) - API 17 (Rev 3)"}},
        {18, {"Jelly Bean", "4.3 (Jelly Bean) - API 18 (Rev 3)"}},
        {19, {"KitKat", "4.4 (KitKat) - API 19 (Rev 4)"}},
        {20, {"KitKat", "4.4 (KitKat Wear) - API 20 (Rev 2)"}},
        {21, {"Lollipop", "5.0 (Lollipop) - API 21 (Rev 2)"}},
        {22, {"Lollipop", "5.1 (Lollipop) - API 22 (Rev 2)"}},
        {23, {"Marshmallow", "6.0 (Marshmallow) - API 23 (Rev 1)"}},
        {24, {"Nougat", "7.0 (Nougat) - API 24"}},
        {25, {"Nougat", "7.1 (Nougat) - API 25"}},
        {26, {"Oreo", "8.0 (Oreo) - API 26"}},
        {27, {"Oreo", "8.1 (Oreo) - API 27"}},
        {28, {"Pie", "9.0 (Pie) - API 28"}},
        {29, {"Q", "10.0 (Q) - API 29"}},
        {30, {"R", "11.0 (R) - API 30"}},
        {31, {"S", "12.0 (S) - API 31"}},
        {32, {"Sv2", "12.0 (S) - API 32"}},
        {33, {"Tiramisu", "13.0 (T) - API 33"}},
        {34, {"UpsideDownCake", "14.0 (U) - API 34"}},
        {35, {"VanillaIceCream", "15.0 (V) - API 35"}},
};

std::string_view getApiDessertName(int apiLevel) {
    auto it = kApiLevelInfo.find(apiLevel);
    if (it != kApiLevelInfo.end()) {
        return it->second.dessertName;
    }
    return "";
}

std::string getFullApiName(int apiLevel) {
    if (apiLevel < 0 || apiLevel > 99) {
        return "Unknown API version";
    }

    auto it = kApiLevelInfo.find(apiLevel);
    if (it != kApiLevelInfo.end()) {
        return std::string(it->second.fullName);
    } else {
        return absl::StrFormat("API %d", apiLevel);
    }
}

int getApiLevelFromDessertName(std::string_view dessertName) {
    for (const auto& [apiLevel, info] : kApiLevelInfo) {
        if (info.dessertName == dessertName) {
            return apiLevel;
        }
    }
    return Avd::kUnknownApiLevel;
}

int getApiLevelFromLetter(char letter) {
    char letterUpper = absl::ascii_toupper(letter);
    for (const auto& [apiLevel, info] : kApiLevelInfo) {
        if (absl::ascii_toupper(info.dessertName[0]) == letterUpper) {
            return apiLevel;
        }
    }
    return Avd::kUnknownApiLevel;
}

int getApiLevel(std::string_view target) {
    int level = Avd::kUnknownApiLevel;

    if (target.empty()) {
        // Use your preferred logging method here.
        return level;
    }

    std::string_view levelStr;
    if (absl::StartsWith(target, "android-")) {
        levelStr = target.substr(8);
    } else {
        std::vector<std::string_view> parts = absl::StrSplit(target, ':');
        if (parts.size() == 3) {
            levelStr = parts[2];
        }
    }

    if (levelStr.empty() || !absl::ascii_isdigit(levelStr[0])) {
        if (!levelStr.empty() && absl::ascii_isalpha(levelStr[0])) {
            if (levelStr.size() == 1) {
                level = getApiLevelFromLetter(levelStr[0]);
            } else {
                level = getApiLevelFromDessertName(levelStr);
            }
        } else {
            // Use your preferred error handling here.
            return Avd::kUnknownApiLevel;
        }
    } else {
        if (!absl::SimpleAtoi(levelStr, &level)) {
            // Handle the error (e.g., log, return default value)
            return Avd::kUnknownApiLevel;
        }

        level = std::max(level, 3);
    }

    return level;
}

std::string getIconForDeviceType(DeviceType flavor) {
    switch (flavor) {
        case DeviceType::kPhone:
            return "📱";  // 📱 (Smartphone)
        case DeviceType::kTv:
            return "📺";  // 📺 (Television)
        case DeviceType::kWear:
            return "⌚️";  // ⌚️ (Smartwatch)
        case DeviceType::kAndroidAuto:
            return "🚗";  // 🚗 (Car)
        case DeviceType::kDesktop:
            return "🖥️";  // 🖥️ (Desktop computer)
        default:
            return "🤷";  // 🤷 (Unknown)
    }
}

}  // namespace

Avd::CpuArchitecture FileBackedAvd::detectArchitecture() const {
    auto abi = mConfig->getString("abi.type", "unknown");
    if (absl::StrContains(abi, "x86")) {
        return CpuArchitecture::kX86;
    }

    if (absl::StrContains(abi, "arm")) {
        return CpuArchitecture::kArm;
    }

    return CpuArchitecture::kUnknown;
}

int FileBackedAvd::apiLevel() const {
    return getApiLevel(mConfig->getString("target", ""));
}

std::string FileBackedAvd::dessert() const {
    return std::string(getApiDessertName(apiLevel()));
}

std::string FileBackedAvd::apiDescription() const {
    return getFullApiName(apiLevel());
}

bool FileBackedAvd::hasEncryptionKey() const {
    return getImageFilePath(Avd::ImageType::ENCRYPTIONKEY).ok();
}

DeviceType FileBackedAvd::getDeviceType() const {
    DeviceType res = DeviceType::kUnknown;

    const std::unordered_map<std::string, DeviceType> labelMap{
            {"phone", DeviceType::kPhone},     {"atv", DeviceType::kTv},
            {"wear", DeviceType::kWear},       {"aw", DeviceType::kWear},
            {"car", DeviceType::kAndroidAuto}, {"pc", DeviceType::kDesktop}};

    const PropertyList props = {"ro.product.name", "ro.product.system.name", "ro.build.flavor"};

    auto buildprop = getSystemImageFilePath(Avd::ImageType::BUILDPROP);
    if (!buildprop.ok()) {
        ABSL_LOG(WARNING) << "Unable to retrieve image path: " << buildprop.status().message()
                          << ", using unknown avd device type.";
        return DeviceType::kUnknown;
    }

    if (!System::get()->pathExists(*buildprop) || !System::get()->pathCanRead(*buildprop)) {
        ABSL_LOG(WARNING) << "Unable to read build properties: " << buildprop->string()
                          << ", using unknown device type.";
        return DeviceType::kUnknown;
    }
    IniFile buildIni(*buildprop);
    buildIni.read();

    for (const auto& prop : props) {
        if (!buildIni.hasKey(prop)) {
            continue;
        }

        auto build = buildIni.getString(prop, "_unused");
        for (const auto& [key, val] : labelMap) {
            if (build.find(key) != std::string::npos) {
                return val;
            }
        }
    }

    // Likely unknown.
    return res;
}

absl::StatusOr<fs::path> FileBackedAvd::getImageFilePath(Avd::ImageType imgType) const {
    fs::path possible = mContentPath / _imageFileNames[static_cast<uint8_t>(imgType)];
    if (System::get()->pathIsFile(possible) && System::get()->pathCanRead(possible)) {
        return possible;
    }
    ABSL_VLOG(1) << "Did not find " << possible << " in " << mContentPath
                 << " falling back to system path";
    return getSystemImageFilePath(imgType);
}

absl::StatusOr<fs::path> FileBackedAvd::getSystemImageFilePath(Avd::ImageType imgType) const {
    auto make_path = [imgType](const fs::path& p) {
        return p / _imageFileNames[static_cast<uint8_t>(imgType)];
    };

    auto check_path = [](const fs::path& p) {
        return System::get()->pathExists(p) && System::get()->pathCanRead(p);
    };

    if (!mSysdirOverride.empty()) {
        if (auto p = make_path(mSysdirOverride); check_path(p)) {
            VLOG(1) << "Found in sysdir override: " << p;
            return p;
        } else {
            return absl::NotFoundError(
                    absl::StrCat("Path ", p.string(), " using sysdir override does not exist"));
        }
    }
    auto sdk = ConfigDirs::getSdkRootDirectory();
    VLOG(1) << "SDK Root path: " << sdk;
    fs::path path = "no-sysimg";
    std::string key;
    for (int n = 0; n < MAX_SEARCH_PATHS; n++) {
        key = absl::StrFormat("%s%d", SEARCH_PREFIX, n);
        if (!mConfig->hasKey(key)) {
            continue;
        }
        if (path = make_path(sdk / mConfig->getString(key, "unused")); check_path(path)) {
            VLOG(1) << "Found in system dir: " << path;
            return path;
        }
        VLOG(1) << "Not found in system dir: " << path;
    }
    return absl::NotFoundError(absl::StrFormat("Path %s specified in %s does not exist (key=%s)",
                                               path.string(), mConfig->getBackingFile().string(),
                                               key));
}

std::string FileBackedAvd::details(const bool verbose) const {
    if (verbose) {
        auto icon = getIconForDeviceType(getDeviceType());
        return absl::StrFormat("%-45s  - (%4dx%4d) %s", mName, mHwCfg.hw_lcd_width,
                               mHwCfg.hw_lcd_height, icon);
    } else {
        return mName;
    }
}

FileBackedAvd::FileBackedAvd(fs::path content_path, std::unique_ptr<IniFile> target,
                             std::unique_ptr<IniFile> config, std::string name,
                             std::string sysdir_override)
    : mName(name),
      mContentPath(content_path),
      mTarget(std::move(target)),
      mConfig(std::move(config)),
      mSysdirOverride(std::move(sysdir_override)) {
    mHwCfg.load(mConfig.get());

    // TODO also load skin hardware.ini if present?

    // TODO this probably needs to be updated when snapshots are supported.
    auto hw_path = mContentPath / CORE_HARDWARE_INI;
    if (auto* sys = System::get(); sys->pathExists(hw_path) && sys->pathCanRead(hw_path)) {
        auto hw_config = std::make_unique<IniFile>(hw_path);
        if (hw_config->read()) {
            // TODO load without defaults.
            mHwCfg.load(hw_config.get());
        }
    }

    mHwCfg.applyDefaults(this);
}

// static
absl::StatusOr<std::unique_ptr<FileBackedAvd>> FileBackedAvd::parse(fs::path ini_file,
                                                                    std::string sysdir_override) {
    auto* sys = System::get();
    if (!sys->pathExists(ini_file) || !sys->pathCanRead(ini_file)) {
        return absl::NotFoundError(absl::StrCat("No access to: ", System::pathAsString(ini_file)));
    }

    auto ini = std::make_unique<IniFile>(ini_file);
    if (!ini->read()) {
        return absl::InternalError(
                absl::StrCat("Unable to parse ini file: ", System::pathAsString(ini_file)));
    }

    // Extract the avd name from the .ini file.
    std::string name = System::pathAsString(ini_file.stem());

    fs::path content_path = fs::path(ini->get<std::string>("path", ""));
    if (!sys->pathExists(content_path) || !sys->pathCanRead(content_path)) {
        auto rel_path = ini->get<std::string>("path.rel", "");
        content_path = ConfigDirs::getUserDirectory() / rel_path;
    }
    fs::path cfg_ini = content_path / "config.ini";

    if (!sys->pathExists(cfg_ini) || !sys->pathCanRead(cfg_ini)) {
        return absl::NotFoundError(absl::StrFormat("Unable to parse %s, no access to config: %s",
                                                   name, cfg_ini.string()));
    }

    auto config = std::make_unique<IniFile>(cfg_ini);
    if (!config->read()) {
        return absl::InternalError("Unable to parse ini file: " + cfg_ini.string());
    }
    return std::unique_ptr<FileBackedAvd>(new FileBackedAvd(
            content_path, std::move(ini), std::move(config), name, std::move(sysdir_override)));
}

namespace {
// Check that an AVD name is valid.
bool _checkAvdName(const std::string& name) {
    int len = strspn(name.c_str(),
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                     "abcdefghijklmnopqrstuvwxyz"
                     "0123456789_.-");
    return (name.size() == len);
}
}  // namespace

// static
std::vector<std::string> Avd::list() {
    std::vector<std::string> avds;
    auto pattern = std::regex(".*.ini");
    auto directory_path = ConfigDirs::getAvdRootDirectory();

    for (const auto& entry : fs::directory_iterator(directory_path)) {
        const auto& filename = entry.path().filename().string();

        // Simple pattern matching
        if (std::regex_match(filename, pattern)) {
            std::string name = filename;
            name.erase(name.size() - 4);
            if (_checkAvdName(name)) {
                avds.push_back(name);
            }
        }
    }
    return avds;
}

// static
absl::StatusOr<std::unique_ptr<Avd>> Avd::fromName(std::string name, std::string sysdir_override) {
    auto directory_path = ConfigDirs::getAvdRootDirectory();
    return FileBackedAvd::parse(directory_path / (name + ".ini"), std::move(sysdir_override));
}

// static
fs::path Avd::getImageFilename(Avd::ImageType imgType) {
    return _imageFileNames[static_cast<uint8_t>(imgType)];
}

}  // namespace android::goldfish
