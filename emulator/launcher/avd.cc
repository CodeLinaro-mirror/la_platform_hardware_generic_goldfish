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
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
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

#include "android/base/system.h"
#include "android/goldfish/hardware_config.h"
#include "android/goldfish/ini_file.h"
#include "android/goldfish/input_paths.h"
#include "android/goldfish/memory_config.h"
#include "android/status/status_macros.h"
#include "avd_keys.h"
#include "goldfish/file/file.h"
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

struct BuildProp {
    std::string Abi() const { return build_ini_.GetString("ro.product.cpu.abi", "unknown"); }

    int ApiLevel() const {
        return build_ini_.GetInt("ro.system.build.version.sdk", Avd::kUnknownApiLevel);
    }

    std::string Sdk() const { return build_ini_.GetString("ro.build.version.sdk", "unknown"); }
    std::string Number() const {
        return build_ini_.GetString("ro.build.version.incremental", "unknown");
    }

    std::string Id() const { return build_ini_.GetString("ro.build.id", "unknown"); }
    std::string Fingerprint() const {
        using namespace std::literals;
        constexpr auto props = std::array{"ro.build.fingerprint"sv, "ro.system.build.fingerprint"sv,
                                          "ro.build.display.id"sv};

        for (const auto& prop : props) {
            if (auto v = build_ini_.GetString(prop, ""sv); !v.empty()) {
                return v;
            }
        }
        return ""s;
    }
    int64_t Timestamp() const { return build_ini_.GetInt64("ro.build.date.utc", 0); }

    std::string Flavour() const { return build_ini_.GetString("ro.build.flavor", "unknown"); }

    std::string ProductName() const {
        using namespace std::literals;
        constexpr auto props =
                std::array{"ro.product.name"sv, "ro.product.system.name"sv, "ro.build.flavor"sv};

        for (const auto& prop : props) {
            if (auto build = build_ini_.GetString(prop); !build.empty()) {
                return build;
            }
        }
        return {};
    }

    IniFile build_ini_;
};

}  // namespace

class FileBackedAvd : public Avd {
  public:
    FileBackedAvd(std::string name, SystemImagePaths system_image_paths, IniFile config_ini,
                  BuildProp build, HardwareConfig hw_cfg, fs::path content_path)
            : name_(std::move(name))
            , system_image_paths_(std::move(system_image_paths))
            , config_ini_(std::move(config_ini))
            , build_ini_(std::move(build))
            , hw_cfg_(std::move(hw_cfg))
            , content_path_(std::move(content_path)) {}

    std::string Name() const override { return name_; }
    const SystemImagePaths& GetSystemImagePaths() const override { return system_image_paths_; }
    fs::path GetContentPath() const override { return content_path_; };
    const HardwareConfig& Hw() const override { return hw_cfg_; }

    bool Playstore() const override { return false; }
    std::string DisplayName() const override {
        return config_ini_.GetString("avd.ini.displayname", Name());
    }
    std::string SkinName() const override { return config_ini_.GetString("skin.name", ""); }
    std::string Id() const override {
        // TODO allow override with opts.id
        return Name();
    }

    std::string Abi() const override {
        // TODO check against detected arch.
        return build_ini_.Abi();
    }

    int ApiLevel() const override {
        // TODO Maybe check config_ini_.GetString("target") e.g. android-36.1 against build_ini_
        // ro.system.build.version.sdk_full.
        return build_ini_.ApiLevel();
    }

    std::string BuildSdk() const override { return build_ini_.Sdk(); }
    std::string BuildId() const override { return build_ini_.Id(); }
    std::string BuildFingerprint() const override { return build_ini_.Fingerprint(); }
    int64_t BuildTimestamp() const override { return build_ini_.Timestamp(); }
    std::string BuildFlavour() const override { return build_ini_.Flavour(); }
    std::string BuildProductName() const override { return build_ini_.ProductName(); }
    std::string BuildNumber() const override { return build_ini_.Number(); }

    Avd::CpuArchitecture DetectArchitecture() const override {
        auto abi = config_ini_.GetString("abi.type", "unknown");
        if (absl::StrContains(abi, "x86")) {
            return CpuArchitecture::kX86;
        }

        if (absl::StrContains(abi, "arm")) {
            return CpuArchitecture::kArm;
        }

        return CpuArchitecture::kUnknown;
    }

    std::string Dessert() const override { return std::string(GetApiDessertName(ApiLevel())); }

    std::string ApiDescription() const override { return GetFullApiName(ApiLevel()); }

    absl::StatusOr<std::optional<int>> GetLastRunQemuVersion() const override {
        auto qemu_version_path = GetContentPath() / AVD_QEMU_VERSION_FILENAME;
        if (!base::file::exists(qemu_version_path)) {
            // File does not exist, not an error
            return std::nullopt;
        }

        std::ifstream ifs(qemu_version_path);
        if (!ifs.is_open()) {
            return absl::PermissionDeniedError(
                    absl::StrCat("Could not open file for reading: ", qemu_version_path.string()));
        }

        std::string file_content;
        ifs >> file_content;

        int value = 0;
        if (file_content.empty() || !absl::SimpleAtoi(file_content, &value)) {
            // File is empty, or invalid
            return absl::InvalidArgumentError(
                    absl::StrCat("File content '", file_content, "' is not a valid integer."));
        }

        return value;
    }

    absl::Status SetLastRunQemuVersion(int version) override {
        auto qemu_version_path = GetContentPath() / AVD_QEMU_VERSION_FILENAME;
        std::ofstream ofs(qemu_version_path, std::ios::trunc);
        if (!ofs.is_open()) {
            return absl::InternalError(
                    absl::StrCat("Failed to open file for writing: ", qemu_version_path.string()));
        }

        ofs << version;
        if (!ofs.good()) {
            return absl::InternalError(
                    absl::StrCat("Failed to write into file: ", qemu_version_path.string()));
        }

        return absl::OkStatus();
    }

    DeviceType GetDeviceType() const override {
        using namespace std::literals;
        constexpr auto label_map = std::array{
            std::pair{"phone"sv, DeviceType::kPhone},     std::pair{"atv"sv, DeviceType::kTv},
            std::pair{"wear"sv, DeviceType::kWear},       std::pair{"aw"sv, DeviceType::kWear},
            std::pair{"car"sv, DeviceType::kAndroidAuto}, std::pair{"pc"sv, DeviceType::kDesktop},
            std::pair{"desktop"sv, DeviceType::kDesktop}, std::pair{"xr"sv, DeviceType::kXr},
            std::pair{"glasses"sv, DeviceType::kGlasses}};

        auto product_name = BuildProductName();
        for (const auto& [key, val] : label_map) {
            if (product_name.contains(key)) {
                return val;
            }
        }
        return DeviceType::kUnknown;
    }

    std::string Details(const bool verbose) const override {
        if (verbose) {
            auto icon = GetIconForDeviceType(GetDeviceType());
            return absl::StrFormat("%s (%s) %s api: %d arch: %s res: %4dx%4d build: %s flavour: %s",
                                   Id(), DisplayName(), icon, ApiLevel(), Abi(),
                                   hw_cfg_.hw_lcd_width, hw_cfg_.hw_lcd_height, BuildNumber(),
                                   BuildFlavour());
        }
        return name_;
    }

  private:
    std::string name_;

    SystemImagePaths system_image_paths_;
    IniFile config_ini_;
    BuildProp build_ini_;
    HardwareConfig hw_cfg_;

    fs::path content_path_;  // Usually ~/.android/avd/<name>.avd/
};

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
    auto pattern = std::regex(".*\\.ini");

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
absl::StatusOr<std::unique_ptr<Avd>> Avd::FromName(const AndroidOptions& opts,
                                                   const android::goldfish::UserPaths& user_paths,
                                                   const std::string& name, bool wipe_data,
                                                   fs::path content_override) {
    auto ini_path = user_paths.avd_directory / (name + ".ini");

    if (!base::file::exists(ini_path) || !base::file::can_read(ini_path)) {
        std::string homeSearchDir =
                (fs::path("$HOME") / ".android" / "avd").make_preferred().string();
        std::string sdkHomeSearchDir =
                (fs::path("$ANDROID_SDK_HOME") / "avd").make_preferred().string();

        std::string envName = "HOME";
        std::string searchDir = homeSearchDir;
        if (!android::base::System::Get()->EnvGet("ANDROID_AVD_HOME").empty()) {
            envName = "ANDROID_AVD_HOME";
            searchDir = "$ANDROID_AVD_HOME";
        } else if (!android::base::System::Get()->EnvGet("ANDROID_SDK_HOME").empty()) {
            envName = "ANDROID_SDK_HOME";
            searchDir = sdkHomeSearchDir;
        }

        return absl::NotFoundError(absl::StrFormat(
                "%s is defined but there is no file %s.ini in %s\n"
                "(Note: Directories are searched in the order $ANDROID_AVD_HOME, %s and %s)",
                envName, name, searchDir, sdkHomeSearchDir, homeSearchDir));
    }

    LOG(INFO) << "Parsing AVD: " << ini_path.string();
    auto ini = std::make_unique<IniFile>(ini_path);
    if (!ini->Read()) {
        return absl::InternalError(absl::StrCat("Unable to parse ini file: ", ini_path.string()));
    }

    constexpr std::string_view kConfigIni = "config.ini";
    constexpr std::string_view kSdCardImg = "sdcard.img";

    fs::path original_content_path;
    if (auto abs_content_path = fs::path(ini->Get<std::string>("path", ""));
        base::file::exists(abs_content_path)) {
        original_content_path = abs_content_path;
    } else if (auto rel_content_path =
                       user_paths.user_directory / ini->Get<std::string>("path.rel", "");
               base::file::exists(rel_content_path)) {
        original_content_path = rel_content_path;
    } else {
        return absl::NotFoundError(absl::StrCat("AVD content directory not found: abs - ",
                                                abs_content_path.string(), " rel - ",
                                                rel_content_path.string()));
    }

    if (!base::file::can_read(original_content_path)) {
        return absl::PermissionDeniedError(
                absl::StrCat("AVD content directory exists but is not readable: ",
                             original_content_path.string()));
    }

    if (!base::file::is_dir(original_content_path)) {
        return absl::InvalidArgumentError(
                absl::StrCat("AVD content directory exists but is not a directory: ",
                             original_content_path.string()));
    }

    if (content_override.empty() && wipe_data) {
        LOG(WARNING) << "Performing factory reset: clearing AVD for an initial cold boot";
        for (auto& path : android::base::file::scan_dir(original_content_path, /*fullPath=*/true)) {
            if (path.filename() == kConfigIni) {
                continue;
            }
            if (path.filename() == kSdCardImg) {
                continue;
            }
            if (auto s = android::base::file::rm_recursive(path); !s.ok()) {
                LOG(ERROR) << "Factory reset failed: unable to remove AVD file " << path.string()
                           << " - " << s;
                return absl::InternalError("AVD wipe data failed");
            }
        }
    }

    auto config_ini_path = original_content_path / kConfigIni;
    if (!base::file::exists(config_ini_path) || !base::file::can_read(config_ini_path)) {
        return absl::NotFoundError(absl::StrCat(
                "Unable to parse ", name, ", no access to config: ", config_ini_path.string()));
    }

    IniFile config_ini(config_ini_path);
    if (!config_ini.Read()) {
        return absl::InternalError(
                absl::StrCat("Unable to parse config ini file: ", config_ini_path.string()));
    }

    std::vector<fs::path> sys_image_search_paths;
    if (opts.sysdir) {
        sys_image_search_paths.push_back(fs::path(opts.sysdir));
    } else {
        for (int n = 0; n < kMaxSearchPaths; n++) {
            if (const std::string s = config_ini.GetString(absl::StrCat(kSearchPrefix, n), "");
                !s.empty()) {
                sys_image_search_paths.push_back(user_paths.sdk_directory / s);
            }
        }
    }

    ASSIGN_OR_RETURN(auto system_image_paths,
                     ResolveSystemImagePaths(sys_image_search_paths, opts));

    IniFile build_ini(system_image_paths.build_properties);
    if (!build_ini.Read()) {
        return absl::InternalError(absl::StrCat("Unable to parse build properties file: ",
                                                system_image_paths.build_properties.string()));
    }
    BuildProp build_wrapper{
        .build_ini_ = std::move(build_ini),
    };

    // check abi

    fs::path content_path = original_content_path;
    if (!content_override.empty()) {
        content_path = content_override;
        if (!base::file::can_read(content_path)) {
            return absl::PermissionDeniedError(
                    absl::StrCat("AVD override content directory exists but is not readable: ",
                                 content_path.string()));
        }

        if (!base::file::is_dir(content_path)) {
            return absl::InvalidArgumentError(
                    absl::StrCat("AVD override content directory exists but is not a directory: ",
                                 content_path.string()));
        }
    }

    if (!base::file::can_write(content_path)) {
        return absl::PermissionDeniedError(absl::StrCat(
                "AVD content directory exists but is not writable: ", content_path.string()));
    }

    if (opts.verbose) {
        LOG(INFO) << "Listing avd content directory (" << content_path << "):";
        for (const auto& path : base::file::scan_dir_recursive(content_path)) {
            LOG(INFO) << "    " << path.lexically_relative(content_path).string();
        }
    }

    HardwareConfig hw_cfg;
    hw_cfg.Load(config_ini);

    // TODO also load skin hardware.ini if present?

    // TODO this probably needs to be updated when snapshots are supported.
    // #define CORE_HARDWARE_INI "hardware-qemu.ini"
    auto hw_path = content_path / CORE_HARDWARE_INI;
    if (base::file::exists(hw_path) && base::file::can_read(hw_path)) {
        auto hw_config = std::make_unique<IniFile>(hw_path);
        if (hw_config->Read()) {
            // TODO load without defaults.
            hw_cfg.Load(*hw_config);
        }
    }

    hw_cfg.ApplyDefaults(user_paths.sdk_directory, user_paths.avd_directory);
    RETURN_IF_ERROR(MemoryConfig::FinalizeRamAndHeapSize(hw_cfg, build_wrapper.ApiLevel()));

    // save to CORE_HARDWARE_INI as well, embedded ui needs it
    auto hw_config = std::make_unique<IniFile>(hw_path);
    hw_cfg.Write(hw_config.get());
    hw_config->WriteDiscardingEmpty();

    return std::make_unique<FileBackedAvd>(name, std::move(system_image_paths),
                                           std::move(config_ini), std::move(build_wrapper),
                                           std::move(hw_cfg), std::move(content_path));
}

// static
fs::path Avd::GetImageFilename(Avd::ImageType img_type) {
    return kImageFileNames[static_cast<uint8_t>(img_type)];
}

}  // namespace android::goldfish
