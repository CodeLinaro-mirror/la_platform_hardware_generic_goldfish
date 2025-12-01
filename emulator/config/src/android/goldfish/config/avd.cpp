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

#include <cctype>
#include <filesystem>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "aemu/base/files/IniFile.h"
#include "android/base/system/File.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/input_paths.h"

#include "host-common/constants.h"

#include "keys.h"

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

using PropertyList = const std::array<std::string, 3>;

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

bool FileBackedAvd::loadBuildProps() {
    auto buildprop = getSystemImageFilePath(Avd::ImageType::BUILDPROP);
    if (!buildprop.ok()) {
        LOG(WARNING) << "Unable to retrieve image path: " << buildprop.status().message()
                          << ", using unknown avd device type.";
        return false;
    }

    if (!base::file::exists(*buildprop) || !base::file::can_read(*buildprop)) {
        LOG(WARNING) << "Unable to read build properties: " << buildprop->string()
                          << ", using unknown device type.";
        return false;
    }
    mBuildIni.setBackingFile(*buildprop);
    return mBuildIni.read();
}

DeviceType FileBackedAvd::getDeviceType() const {
    DeviceType res = DeviceType::kUnknown;

    const std::unordered_map<std::string, DeviceType> labelMap{
            {"phone", DeviceType::kPhone},     {"atv", DeviceType::kTv},
            {"wear", DeviceType::kWear},       {"aw", DeviceType::kWear},
            {"car", DeviceType::kAndroidAuto}, {"pc", DeviceType::kDesktop}};

    const PropertyList props = {"ro.product.name", "ro.product.system.name", "ro.build.flavor"};

    for (const auto& prop : props) {
        if (!mBuildIni.hasKey(prop)) {
            continue;
        }

        auto build = mBuildIni.getString(prop, "_unused");
        for (const auto& [key, val] : labelMap) {
            if (build.find(key) != std::string::npos) {
                return val;
            }
        }
    }

    // Likely unknown.
    return res;
}

absl::StatusOr<fs::path> FileBackedAvd::getSystemImageFilePath(Avd::ImageType imgType) const {
    auto image_file_name = getImageFilename(imgType);

    auto check_path = [](const fs::path& p) {
        return base::file::exists(p) && base::file::can_read(p);
    };

    VLOG(1) << "Searching for sys image: " << image_file_name;
    fs::path path = "no-sysimg";
    for (const auto &sys_path: mSysImagePaths) {
        if (path = sys_path / image_file_name; check_path(path)) {
            VLOG(1) << "Found image in system dir: " << path;
            return path;
        }
        VLOG(1) << "Not found in system dir: " << path;
    }
    return absl::NotFoundError(absl::StrCat("System image not found: ", image_file_name.string(), " (last checked ", path.string(), ")"));
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

FileBackedAvd::FileBackedAvd(std::string name, std::unique_ptr<IniFile> config, fs::path sdk_path, fs::path avd_path, fs::path content_path, std::vector<fs::path> sys_image_paths)
        : mName(name)
        , mConfig(std::move(config))
        , mSdkPath(std::move(sdk_path))
        , mAvdPath(std::move(avd_path))
        , mContentPath(std::move(content_path))
        , mSysImagePaths(std::move(sys_image_paths)) {
    if (!loadBuildProps()) {
        LOG(ERROR) << "Failed to load build properties from file";
    }
    // check abi

    mHwCfg.load(*mConfig);

    // TODO also load skin hardware.ini if present?

    // TODO this probably needs to be updated when snapshots are supported.
    auto hw_path = getContentPath() / CORE_HARDWARE_INI;
    if (base::file::exists(hw_path) && base::file::can_read(hw_path)) {
        auto hw_config = std::make_unique<IniFile>(hw_path);
        if (hw_config->read()) {
            // TODO load without defaults.
            mHwCfg.load(*hw_config);
        }
    }

    mHwCfg.applyDefaults(getSdkPath(), getAvdPath());

    // save to CORE_HARDWARE_INI as well, embedded ui needs it
    {
        auto hw_config = std::make_unique<IniFile>(hw_path);
        mHwCfg.write(hw_config.get());
        hw_config->writeDiscardingEmpty();
    }
}

// static
absl::StatusOr<std::unique_ptr<FileBackedAvd>> FileBackedAvd::parse(std::string name, fs::path config_ini_path, fs::path sdk_path, fs::path avd_path, fs::path content_path, fs::path sysdir_override) {
    if (!base::file::exists(config_ini_path) || !base::file::can_read(config_ini_path)) {
        return absl::NotFoundError(absl::StrCat("Unable to parse ", name, ", no access to config: ", config_ini_path.string()));
    }

    auto config = std::make_unique<IniFile>(config_ini_path);
    if (!config->read()) {
        return absl::InternalError(absl::StrCat("Unable to parse ini file: ", config_ini_path.string()));
    }

    std::vector<fs::path> sys_image_paths;
    if (!sysdir_override.empty()) {
        sys_image_paths.push_back(sysdir_override);
    } else {
        for (int n = 0; n < MAX_SEARCH_PATHS; n++) {
            if (std::string s = config->getString(absl::StrCat(SEARCH_PREFIX, n), ""); !s.empty()) {
                sys_image_paths.push_back(sdk_path / s);
            }
        }
    }

    return std::unique_ptr<FileBackedAvd>(new FileBackedAvd(std::move(name), std::move(config), std::move(sdk_path), std::move(avd_path), std::move(content_path), std::move(sys_image_paths)));
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
std::vector<std::string> Avd::list(const fs::path &avd_directory) {
    std::vector<std::string> avds;
    auto pattern = std::regex(".*.ini");

    for (const auto& entry : fs::directory_iterator(avd_directory)) {
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
absl::StatusOr<std::unique_ptr<Avd>> Avd::fromName(const android::goldfish::ResolvedInputPaths &paths, std::string name, fs::path sysdir_override,
                                                   fs::path writable_content_override) {

    auto ini_path = paths.avd_directory / (name + ".ini");

    if (!base::file::exists(ini_path) || !base::file::can_read(ini_path)) {
        return absl::NotFoundError(absl::StrCat("No access to: ", ini_path.string()));
    }

    auto ini = std::make_unique<IniFile>(ini_path);
    if (!ini->read()) {
        return absl::InternalError(absl::StrCat("Unable to parse ini file: ", ini_path.string()));
    }

    fs::path content_path = fs::path(ini->get<std::string>("path", ""));
    if (!base::file::exists(content_path) || !base::file::can_read(content_path)) {
        auto rel_path = ini->get<std::string>("path.rel", "");
        content_path = paths.user_directory / rel_path;
    }
    fs::path config_ini_path = content_path / "config.ini";

    if (!writable_content_override.empty()) {
        content_path = std::move(writable_content_override);
    }

    return FileBackedAvd::parse(name, config_ini_path, paths.sdk_directory, paths.avd_directory, std::move(content_path), std::move(sysdir_override));
}

// static
fs::path Avd::getImageFilename(Avd::ImageType imgType) {
    return _imageFileNames[static_cast<uint8_t>(imgType)];
}

}  // namespace android::goldfish
