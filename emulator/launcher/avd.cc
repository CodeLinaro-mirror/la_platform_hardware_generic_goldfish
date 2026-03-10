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

#include "android/goldfish/avd.h"

#include <cctype>
#include <filesystem>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>  // For std::move
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

#include "goldfish/file/file.h"
#include "android/goldfish/hardware_config.h"
#include "android/goldfish/ini_file.h"
#include "android/goldfish/input_paths.h"
#include "android/goldfish/memory_config.h"
#include "android/status/status_macros.h"
#include "avd_keys.h"
#include "host-common/constants.h"

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

const std::string_view kImageFileNames[static_cast<int>(Avd::ImageType::AVD_IMAGE_MAX)] = {
#define _AVD_IMG(x, y, z) y,
    AVD_IMAGE_LIST
#undef _AVD_IMG
};

struct ApiLevelInfo {
    std::string_view dessert_name;
    std::string_view full_name;
};

const absl::flat_hash_map<int, ApiLevelInfo> kApiLevelInfo = {
    {10, {.dessert_name = "Gingerbread", .full_name = "2.3.3 (Gingerbread) - API 10 (Rev 2)"}},
    {14,
     {.dessert_name = "Ice Cream Sandwich",
      .full_name = "4.0 (Ice Cream Sandwich) - API 14 (Rev 4)"}},
    {15,
     {.dessert_name = "Ice Cream Sandwich",
      .full_name = "4.0.3 (Ice Cream Sandwich) - API 15 (Rev 5)"}},
    {16, {.dessert_name = "Jelly Bean", .full_name = "4.1 (Jelly Bean) - API 16 (Rev 5)"}},
    {17, {.dessert_name = "Jelly Bean", .full_name = "4.2 (Jelly Bean) - API 17 (Rev 3)"}},
    {18, {.dessert_name = "Jelly Bean", .full_name = "4.3 (Jelly Bean) - API 18 (Rev 3)"}},
    {19, {.dessert_name = "KitKat", .full_name = "4.4 (KitKat) - API 19 (Rev 4)"}},
    {20, {.dessert_name = "KitKat", .full_name = "4.4 (KitKat Wear) - API 20 (Rev 2)"}},
    {21, {.dessert_name = "Lollipop", .full_name = "5.0 (Lollipop) - API 21 (Rev 2)"}},
    {22, {.dessert_name = "Lollipop", .full_name = "5.1 (Lollipop) - API 22 (Rev 2)"}},
    {23, {.dessert_name = "Marshmallow", .full_name = "6.0 (Marshmallow) - API 23 (Rev 1)"}},
    {24, {.dessert_name = "Nougat", .full_name = "7.0 (Nougat) - API 24"}},
    {25, {.dessert_name = "Nougat", .full_name = "7.1 (Nougat) - API 25"}},
    {26, {.dessert_name = "Oreo", .full_name = "8.0 (Oreo) - API 26"}},
    {27, {.dessert_name = "Oreo", .full_name = "8.1 (Oreo) - API 27"}},
    {28, {.dessert_name = "Pie", .full_name = "9.0 (Pie) - API 28"}},
    {29, {.dessert_name = "Q", .full_name = "10.0 (Q) - API 29"}},
    {30, {.dessert_name = "R", .full_name = "11.0 (R) - API 30"}},
    {31, {.dessert_name = "S", .full_name = "12.0 (S) - API 31"}},
    {32, {.dessert_name = "Sv2", .full_name = "12.0 (S) - API 32"}},
    {33, {.dessert_name = "Tiramisu", .full_name = "13.0 (T) - API 33"}},
    {34, {.dessert_name = "UpsideDownCake", .full_name = "14.0 (U) - API 34"}},
    {35, {.dessert_name = "VanillaIceCream", .full_name = "15.0 (V) - API 35"}},
};

std::string_view GetApiDessertName(int api_level) {
    auto it = kApiLevelInfo.find(api_level);
    if (it != kApiLevelInfo.end()) {
        return it->second.dessert_name;
    }
    return "";
}

std::string GetFullApiName(int api_level) {
    if (api_level < 0 || api_level > 99) {
        return "Unknown API version";
    }

    auto it = kApiLevelInfo.find(api_level);
    if (it != kApiLevelInfo.end()) {
        return std::string(it->second.full_name);
    }
    return absl::StrFormat("API %d", api_level);
}

int GetApiLevelFromDessertName(std::string_view dessert_name) {
    for (const auto& [api_level, info] : kApiLevelInfo) {
        if (info.dessert_name == dessert_name) {
            return api_level;
        }
    }
    return Avd::kUnknownApiLevel;
}

int GetApiLevelFromLetter(char letter) {
    const char letter_upper = absl::ascii_toupper(letter);
    for (const auto& [api_level, info] : kApiLevelInfo) {
        if (absl::ascii_toupper(info.dessert_name[0]) == letter_upper) {
            return api_level;
        }
    }
    return Avd::kUnknownApiLevel;
}

int GetApiLevel(std::string_view target) {
    int level = Avd::kUnknownApiLevel;

    if (target.empty()) {
        // Use your preferred logging method here.
        return level;
    }

    std::string_view level_str;
    if (absl::StartsWith(target, "android-")) {
        level_str = target.substr(8);
    } else {
        std::vector<std::string_view> parts = absl::StrSplit(target, ':');
        if (parts.size() == 3) {
            level_str = parts[2];
        }
    }

    if (level_str.empty() || !absl::ascii_isdigit(level_str[0])) {
        if (!level_str.empty() && absl::ascii_isalpha(level_str[0])) {
            if (level_str.size() == 1) {
                level = GetApiLevelFromLetter(level_str[0]);
            } else {
                level = GetApiLevelFromDessertName(level_str);
            }
        } else {
            // Use your preferred error handling here.
            return Avd::kUnknownApiLevel;
        }
    } else {
        if (!absl::SimpleAtoi(level_str, &level)) {
            // Handle the error (e.g., log, return default value)
            return Avd::kUnknownApiLevel;
        }

        level = std::max(level, 3);
    }

    return level;
}

std::string GetIconForDeviceType(DeviceType flavor) {
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

Avd::CpuArchitecture FileBackedAvd::DetectArchitecture() const {
    auto abi = config_->GetString("abi.type", "unknown");
    if (absl::StrContains(abi, "x86")) {
        return CpuArchitecture::kX86;
    }

    if (absl::StrContains(abi, "arm")) {
        return CpuArchitecture::kArm;
    }

    return CpuArchitecture::kUnknown;
}

int FileBackedAvd::ApiLevel() const {
    return GetApiLevel(config_->GetString("target", ""));
}

std::string FileBackedAvd::Dessert() const {
    return std::string(GetApiDessertName(ApiLevel()));
}

std::string FileBackedAvd::ApiDescription() const {
    return GetFullApiName(ApiLevel());
}

absl::Status FileBackedAvd::Finalize() {
    RETURN_IF_ERROR(MemoryConfig::FinalizeRamAndHeapSize(hw_cfg_, ApiLevel()));

    // save to CORE_HARDWARE_INI as well, embedded ui needs it
    auto hw_path = GetContentPath() / CORE_HARDWARE_INI;
    auto hw_config = std::make_unique<IniFile>(hw_path);
    hw_cfg_.Write(hw_config.get());
    hw_config->WriteDiscardingEmpty();
    return absl::OkStatus();
}

bool FileBackedAvd::LoadBuildProps() {
    auto buildprop = GetSystemImageFilePath(Avd::ImageType::BUILDPROP);
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
    build_ini_.SetBackingFile(*buildprop);
    return build_ini_.Read();
}

DeviceType FileBackedAvd::GetDeviceType() const {
    const DeviceType res = DeviceType::kUnknown;

    const std::unordered_map<std::string, DeviceType> label_map{
        {"phone", DeviceType::kPhone},     {"atv", DeviceType::kTv},
        {"wear", DeviceType::kWear},       {"aw", DeviceType::kWear},
        {"car", DeviceType::kAndroidAuto}, {"pc", DeviceType::kDesktop}};

    const PropertyList props = {"ro.product.name", "ro.product.system.name", "ro.build.flavor"};

    for (const auto& prop : props) {
        if (!build_ini_.HasKey(prop)) {
            continue;
        }

        auto build = build_ini_.GetString(prop, "_unused");
        for (const auto& [key, val] : label_map) {
            if (build.find(key) != std::string::npos) {
                return val;
            }
        }
    }

    // Likely unknown.
    return res;
}

absl::StatusOr<fs::path> FileBackedAvd::GetSystemImageFilePath(Avd::ImageType img_type) const {
    auto image_file_name = GetImageFilename(img_type);

    auto check_path = [](const fs::path& p) {
        return base::file::exists(p) && base::file::can_read(p);
    };

    VLOG(1) << "Searching for sys image: " << image_file_name;
    fs::path path = "no-sysimg";
    for (const auto& sys_path : sys_image_paths_) {
        if (path = sys_path / image_file_name; check_path(path)) {
            VLOG(1) << "Found image in system dir: " << path;
            return path;
        }
        VLOG(1) << "Not found in system dir: " << path;
    }
    return absl::NotFoundError(absl::StrCat("System image not found: ", image_file_name.string(),
                                            " (last checked ", path.string(), ")"));
}

std::string FileBackedAvd::Details(const bool verbose) const {
    if (verbose) {
        auto icon = GetIconForDeviceType(GetDeviceType());
        return absl::StrFormat("%-45s  - (%4dx%4d) %s", name_, hw_cfg_.hw_lcd_width,
                               hw_cfg_.hw_lcd_height, icon);
    }
    return name_;
}

FileBackedAvd::FileBackedAvd(std::string name, std::unique_ptr<IniFile> config, fs::path sdk_path,
                             fs::path avd_path, fs::path content_path,
                             std::vector<fs::path> sys_image_paths)
        : name_(std::move(name))
        , config_(std::move(config))
        , sdk_path_(std::move(sdk_path))
        , avd_path_(std::move(avd_path))
        , content_path_(std::move(content_path))
        , sys_image_paths_(std::move(sys_image_paths)) {
    if (!LoadBuildProps()) {
        LOG(ERROR) << "Failed to load build properties from file";
    }
    // check abi

    hw_cfg_.Load(*config_);

    // TODO also load skin hardware.ini if present?

    // TODO this probably needs to be updated when snapshots are supported.
    auto hw_path = GetContentPath() / CORE_HARDWARE_INI;
    if (base::file::exists(hw_path) && base::file::can_read(hw_path)) {
        auto hw_config = std::make_unique<IniFile>(hw_path);
        if (hw_config->Read()) {
            // TODO load without defaults.
            hw_cfg_.Load(*hw_config);
        }
    }

    hw_cfg_.ApplyDefaults(GetSdkPath(), GetAvdPath());
}

// static
absl::StatusOr<std::unique_ptr<FileBackedAvd>> FileBackedAvd::Parse(
        std::string name, const fs::path& config_ini_path, fs::path sdk_path, fs::path avd_path,
        fs::path content_path, const fs::path& sysdir_override) {
    if (!base::file::exists(config_ini_path) || !base::file::can_read(config_ini_path)) {
        return absl::NotFoundError(absl::StrCat(
                "Unable to parse ", name, ", no access to config: ", config_ini_path.string()));
    }

    auto config = std::make_unique<IniFile>(config_ini_path);
    if (!config->Read()) {
        return absl::InternalError(
                absl::StrCat("Unable to parse ini file: ", config_ini_path.string()));
    }

    std::vector<fs::path> sys_image_paths;
    if (!sysdir_override.empty()) {
        sys_image_paths.push_back(sysdir_override);
    } else {
        for (int n = 0; n < kMaxSearchPaths; n++) {
            if (const std::string s = config->GetString(absl::StrCat(kSearchPrefix, n), "");
                !s.empty()) {
                sys_image_paths.push_back(sdk_path / s);
            }
        }
    }

    return std::unique_ptr<FileBackedAvd>(new FileBackedAvd(
            std::move(name), std::move(config), std::move(sdk_path), std::move(avd_path),
            std::move(content_path), std::move(sys_image_paths)));
}

namespace {
// Check that an AVD name is valid.
bool CheckAvdName(const std::string& name) {
    const int len = static_cast<int>(strspn(name.c_str(),
                                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                            "abcdefghijklmnopqrstuvwxyz"
                                            "0123456789_.-"));
    return (name.size() == len);
}
}  // namespace

// static
std::vector<std::string> Avd::List(const fs::path& avd_directory) {
    std::vector<std::string> avds;
    auto pattern = std::regex(".*.ini");

    for (const auto& entry : base::file::scan_dir(avd_directory)) {
        const auto& filename = entry.filename().string();

        // Simple pattern matching
        if (std::regex_match(filename, pattern)) {
            std::string name = filename;
            name.erase(name.size() - 4);
            if (CheckAvdName(name)) {
                avds.push_back(name);
            }
        }
    }
    return avds;
}

// static
absl::StatusOr<std::unique_ptr<Avd>> Avd::FromName(
        const android::goldfish::ResolvedInputPaths& paths, const std::string& name, bool wipe_data,
        const fs::path& sysdir_override, fs::path writable_content_override) {
    auto ini_path = paths.avd_directory / (name + ".ini");

    if (!base::file::exists(ini_path) || !base::file::can_read(ini_path)) {
        return absl::NotFoundError(absl::StrCat("No access to: ", ini_path.string()));
    }

    auto ini = std::make_unique<IniFile>(ini_path);
    if (!ini->Read()) {
        return absl::InternalError(absl::StrCat("Unable to parse ini file: ", ini_path.string()));
    }

    fs::path content_path = fs::path(ini->Get<std::string>("path", ""));
    if (!base::file::exists(content_path) || !base::file::can_read(content_path)) {
        auto rel_path = ini->Get<std::string>("path.rel", "");
        content_path = paths.user_directory / rel_path;
    }
    constexpr std::string_view kConfigIni = "config.ini";
    constexpr std::string_view kSdCardImg = "sdcard.img";

    const fs::path config_ini_path = content_path / kConfigIni;

    if (!writable_content_override.empty()) {
        content_path = std::move(writable_content_override);
    } else if (wipe_data) {
        LOG(WARNING) << "Performing factory reset: clearing AVD for an initial cold boot";
        for (auto& path : android::base::file::scan_dir(content_path, /*fullPath=*/true)) {
            if (path.filename() == kConfigIni) {
                continue;
            }
            if (path.filename() == kSdCardImg) {
                continue;
            }
            if (auto s = android::base::file::rm_recursive(path); !s.ok()) {
                LOG(ERROR) << "Factory reset failed: unable to remove AVD file " << path.string() << " - " << s;
                return absl::InternalError("AVD wipe data failed");
            }
        }
    }

    auto avd_res =
            FileBackedAvd::Parse(name, config_ini_path, paths.sdk_directory, paths.avd_directory,
                                 std::move(content_path), sysdir_override);
    if (!avd_res.ok()) {
        return avd_res.status();
    }
    auto avd = std::move(*avd_res);
    RETURN_IF_ERROR(avd->Finalize());
    return avd;
}

// static
fs::path Avd::GetImageFilename(Avd::ImageType img_type) {
    return kImageFileNames[static_cast<uint8_t>(img_type)];
}

}  // namespace android::goldfish
