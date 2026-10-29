// Copyright (C) 2026 The Android Open Source Project
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

#include "android/goldfish/feature_flags.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

#include "goldfish/file/file.h"

namespace android::goldfish {
namespace {

enum class FeatureStatus {
    On,
    Off,
    Default,
};

using FeatureFlagMap = absl::flat_hash_map<EmulatorFeatureFlag, FeatureStatus>;

std::string NormalizeFeatureName(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        if (c != '_') {
            result.push_back(absl::ascii_tolower(static_cast<unsigned char>(c)));
        }
    }
    return result;
}

bool IsIgnoredLegacy(EmulatorFeatureFlag feature) {
    using S = android_studio::EmulatorFeatureFlagState;
    switch (feature) {
    case S::EMULATOR_FEATURE_FLAG_UNSPECIFIED:
    case S::GL_PIPE_CHECKSUM:
    case S::GRALLOC_SYNC:
    case S::ENCRYPT_USER_DATA:
    case S::INTEL_PERFORMANCE_MONITORING_UNIT:
    case S::GL_ASYNC_SWAP:
    case S::GLDMA:
    case S::GLES_DYNAMIC_VERSION:
    case S::WIFI:
    case S::LOGCAT_PIPE:
    case S::HYPERV:
    case S::KVM:
    case S::HAXM:
    case S::HVF:
    case S::WINDOWS_HYPERVISOR_PLATFORM:
    case S::FAST_SNAPSHOT_V1:
    case S::SCREEN_RECORDING:
    case S::VIRTUAL_SCENE:
    case S::SYSTEM_AS_ROOT:
    case S::DEPRECATED_IGNORE_HOST_OPENGL_ERRORS:
    case S::GENERIC_SNAPSHOTS_UI:
    case S::ALLOW_SNAPSHOT_MIGRATION:
    case S::DEPRECATED_GENERIC_INCREMENTAL_SNAPSHOT:
    case S::WINDOWS_ON_DEMAND_SNAPSHOT_LOAD:
    case S::KERNEL_DEVICE_TREE_BLOB_SUPPORT:
    case S::LOCATION_UI_V2:
    case S::GENERIC_INCREMENTAL_SNAPSHOT:
    case S::SNAPSHOT_ADB:
    case S::OFFWORLD:
    case S::OFFWORLD_DISABLE_SECURITY:
    case S::QUICKBOOT_FILE_BACKED:
    case S::REFCOUNT_PIPE:
    case S::GLDMA2:
    case S::HOST_COMPOSITION_V1:
    case S::ON_DEMAND_SNAPSHOT_LOAD:
    case S::WIFI_CONFIGURABLE:
    case S::GL_DIRECT_MEM:
    case S::VIDEO_PLAYBACK:
    case S::VULKAN:
    case S::MACRO_UI:
    case S::CAR_VHAL_TABLE:
    case S::IP_DISCONNECT_ON_LOAD:
    case S::VIRTIO_INPUT:
    case S::MULTI_DISPLAY:
    case S::VULKAN_NULL_OPTIONAL_STRINGS:
    case S::DYNAMIC_MEDIA_PROFILE:
    case S::YUV420_888_to_NV21:
    case S::YUV_Cache:
    case S::KEYCODE_FORWARDING:
    case S::VULKAN_IGNORED_HANDLES:
    case S::VIRTIO_GPU_NEXT:
    case S::HAS_SHARED_SLOTS_HOST_MEMORY_ALLOCATOR:
    case S::CAR_VHAL_REPLAY:
    case S::CAR_ASSIST_BUTTON:
    case S::HARDWARE_DECODER:
    case S::NO_DELAY_CLOSE_COLOR_BUFFER:
    case S::NO_DEVICE_FRAME:
    case S::VIRTIO_GPU_NATIVE_SYNC:
    case S::VIRTIO_WIFI:
    case S::VULKAN_SHADER_FLOAT16_INT8:
    case S::CAR_ROTARY:
    case S::MODEM_SIMULATOR:
    case S::TV_REMOTE:
    case S::NATIVE_TEXTURE_DECOMPRESSION:
    case S::BPTC_TEXTURE_SUPPORT:
    case S::GUEST_USES_ANGLE:
    case S::VIRTIO_VSOCK_PIPE:
    case S::S3TC_TEXTURE_SUPPORT:
    case S::VIRTIO_MOUSE:
    case S::VIRTCONSOLE_LOGCAT:
    case S::VULKAN_QUEUE_SUBMIT_WITH_COMMANDS:
    case S::NO_DRAW:
    case S::GNSS_GRPC_V1:
    case S::MIGRATABLE_SNAPSHOT_SAVE:
    case S::ANDROIDBOOT_PROPS:
    case S::DEVICESKINOVERLAY:
    case S::ANDROIDBOOT_PROPS2:
    case S::BLUETOOTH_EMULATION:
    case S::DEVICESTATE_ON_BOOT:
    case S::HWC_MULTI_CONFIGS:
    case S::ASYNC_COMPOSE_SUPPORT:
    case S::VIRTIO_SND_CARD:
    case S::VIRTIO_TABLET:
    case S::RGTC_TEXTURE_SUPPORT:
    case S::VULKAN_NATIVE_SWAPCHAIN:
    case S::UX_TEST_2024:
        return true;
    default:
        return false;
    }
}

bool IsGuestFeature(EmulatorFeatureFlag feature) {
    using S = android_studio::EmulatorFeatureFlagState;
    switch (feature) {
    case S::GRALLOC_SYNC:
    case S::ENCRYPT_USER_DATA:
    case S::INTEL_PERFORMANCE_MONITORING_UNIT:
    case S::GL_ASYNC_SWAP:
    case S::GLDMA:
    case S::GLDMA2:
    case S::GL_DIRECT_MEM:
    case S::GLES_DYNAMIC_VERSION:
    case S::WIFI:
    case S::PLAY_STORE_IMAGE:
    case S::LOGCAT_PIPE:
    case S::SYSTEM_AS_ROOT:
    case S::KERNEL_DEVICE_TREE_BLOB_SUPPORT:
    case S::DYNAMIC_PARTITION:
    case S::REFCOUNT_PIPE:
    case S::HOST_COMPOSITION_V1:
    case S::WIFI_CONFIGURABLE:
    case S::VIRTIO_INPUT:
    case S::MULTI_DISPLAY:
    case S::VULKAN_NULL_OPTIONAL_STRINGS:
    case S::YUV420_888_to_NV21:
    case S::YUV_Cache:
    case S::KEYCODE_FORWARDING:
    case S::VULKAN_IGNORED_HANDLES:
    case S::VIRTIO_GPU_NEXT:
    case S::MAC80211HWSIM_USERSPACE_MANAGED:
    case S::HARDWARE_DECODER:
    case S::VIRTIO_WIFI:
    case S::MODEM_SIMULATOR:
    case S::VIRTIO_MOUSE:
    case S::VIRTCONSOLE_LOGCAT:
    case S::VIRTIO_VSOCK_PIPE:
    case S::VULKAN_QUEUE_SUBMIT_WITH_COMMANDS:
    case S::VULKAN_BATCHED_DESCRIPTOR_SET_UPDATE:
    case S::MINIGBM:
    case S::GNSS_GRPC_V1:
    case S::ANDROIDBOOT_PROPS:
    case S::ANDROIDBOOT_PROPS2:
    case S::DEVICESKINOVERLAY:
    case S::BLUETOOTH_EMULATION:
    case S::DEVICESTATE_ON_BOOT:
    case S::HWC_MULTI_CONFIGS:
    case S::VIRTIO_SND_CARD:
    case S::VIRTIO_TABLET:
    case S::VSOCK_SNAPSHOT_LOAD_FIXED_B231345789:
    case S::DOWNLOADABLE_SNAPSHOT:
    case S::SUPPORT_PIXEL_FOLD:
    case S::DEVICE_KEYBOARD_HAS_ASSIST_KEY:
    case S::UWB:
    case S::GUEST_ANGLE:
    case S::ANDROID_VIRTUALIZATION_FRAMEWORK:
    case S::XR_MODE_UI:
    case S::XR_MODE_GLASSES_UI:
    case S::MICROPHONE_TOGGLE_UI:
    case S::VIRTIO_DUAL_MODE_MOUSE:
    case S::DUAL_MODE_MOUSE_DISPLAY_HOST_CURSOR:
    case S::ALL_APPS_FOR_HOME_TRAY:
    case S::QEMU_CAMERA_SENSOR_ORIENTATION:
    case S::XR_DIMMING:
    case S::XR_HAND_AND_EYE_POINTERS:
    case S::XR_STATUS_LED:
        return true;
    default:
        return false;
    }
}

std::string FeatureToString(EmulatorFeatureFlag feature) {
    using S = android_studio::EmulatorFeatureFlagState;
    switch (feature) {
    case S::HOST_COMPOSITION_V1:
        return "hostcomposition";
    case S::DEVICE_KEYBOARD_QT_RAW_INPUT:
        return "qtrawkeyboardinput";
    default:
        return NormalizeFeatureName(S::EmulatorFeatureFlag_Name(feature));
    }
}

const absl::flat_hash_map<std::string, EmulatorFeatureFlag>& AllFeaturesByLowerName() {
    static const auto* const kFeatures = [] {
        auto* f = new absl::flat_hash_map<std::string, EmulatorFeatureFlag>();
        for (int i = android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_MIN;
             i <= android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_MAX; ++i) {
            if (!android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_IsValid(i)) {
                continue;
            }
            const auto feature = static_cast<EmulatorFeatureFlag>(i);
            if (feature ==
                android_studio::EmulatorFeatureFlagState::EMULATOR_FEATURE_FLAG_UNSPECIFIED) {
                continue;
            }
            (*f)[FeatureToString(feature)] = feature;
            (*f)[NormalizeFeatureName(
                    android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_Name(feature))] =
                    feature;
        }
        return f;
    }();
    return *kFeatures;
}

const std::vector<EmulatorFeatureFlag>& ActiveFeatures() {
    static const auto* const kActive = [] {
        auto* v = new std::vector<EmulatorFeatureFlag>();
        for (int i = android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_MIN;
             i <= android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_MAX; ++i) {
            if (!android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_IsValid(i)) {
                continue;
            }
            const auto feature = static_cast<EmulatorFeatureFlag>(i);
            if (!IsIgnoredLegacy(feature)) {
                v->push_back(feature);
            }
        }
        return v;
    }();
    return *kActive;
}

std::optional<EmulatorFeatureFlag> StringToFeature(std::string_view name) {
    const auto& features = AllFeaturesByLowerName();
    if (auto it = features.find(NormalizeFeatureName(name)); it != features.end()) {
        return it->second;
    }
    return std::nullopt;
}

FeatureStatus StringToFeatureStatus(std::string_view status_str) {
    if (absl::EqualsIgnoreCase(status_str, "on")) {
        return FeatureStatus::On;
    }
    if (absl::EqualsIgnoreCase(status_str, "off")) {
        return FeatureStatus::Off;
    }
    return FeatureStatus::Default;
}

void SortFeatures(std::vector<EmulatorFeatureFlag>& features) {
    std::sort(features.begin(), features.end(), [](EmulatorFeatureFlag a, EmulatorFeatureFlag b) {
        return static_cast<int>(a) < static_cast<int>(b);
    });
}

absl::StatusOr<FeatureFlagMap> ParseFeatureFile(const std::filesystem::path& filepath,
                                                std::vector<std::string>* unknown_features) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return absl::NotFoundError(
                absl::StrCat("Could not open feature file: ", filepath.string()));
    }

    FeatureFlagMap f;
    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;
        std::string_view trimmed_line = absl::StripAsciiWhitespace(line);
        // Skip empty lines and lines starting with '#' (comments).
        if (trimmed_line.empty() || trimmed_line[0] == '#') {
            continue;
        }

        std::pair<std::string_view, std::string_view> parts = absl::StrSplit(trimmed_line, "=");
        std::string_view name = absl::StripAsciiWhitespace(parts.first);
        std::string_view status = absl::StripAsciiWhitespace(parts.second);
        if (name.empty()) {
            LOG(WARNING) << "Malformed line " << line_num
                         << " in feature file (empty feature name): '" << trimmed_line << "'";
            continue;
        }

        if (status.empty()) {
            LOG(WARNING) << "Malformed line " << line_num
                         << " in feature file (missing or empty feature status): '" << trimmed_line
                         << "'";
            continue;
        }

        if (auto feature = StringToFeature(name); feature.has_value()) {
            if (IsIgnoredLegacy(*feature)) {
                VLOG(1) << "ignoring legacy feature: " << name;
                continue;
            }
            VLOG(1) << "found feature: " << name << " " << status;
            f[*feature] = StringToFeatureStatus(status);
        } else {
            VLOG(1) << "unknown feature: " << name;
            if (unknown_features != nullptr) {
                unknown_features->emplace_back(name);
            }
        }
    }

    return f;
}

}  // namespace

FeatureFlags::FeatureFlags() {
    for (EmulatorFeatureFlag feature : ActiveFeatures()) {
        features_.emplace(feature, FeatureState{});
    }
}

FeatureFlags FeatureFlags::CreateForTesting(
        std::initializer_list<EmulatorFeatureFlag> baseline_enabled) {
    FeatureFlags flags;
    for (EmulatorFeatureFlag feature : baseline_enabled) {
        if (auto it = flags.features_.find(feature); it != flags.features_.end()) {
            auto& [flag, state] = *it;
            state.baseline_val = true;
            state.current_val = true;
        }
    }
    return flags;
}

absl::StatusOr<FeatureFlags> FeatureFlags::Create(const Options& options) {
    FeatureFlags flags;

    FeatureFlagMap host_ini_map;
    if (!options.host_ini_path.empty()) {
        std::vector<std::string> unknown_host_features;
        auto host_ini = ParseFeatureFile(options.host_ini_path, &unknown_host_features);
        if (!host_ini.ok()) {
            return host_ini.status();
        }
        if (!unknown_host_features.empty()) {
            return absl::InvalidArgumentError(
                    absl::StrCat("Unknown feature(s) in ", options.host_ini_path.string(), ": ",
                                 absl::StrJoin(unknown_host_features, ", ")));
        }
        host_ini_map = std::move(*host_ini);
    }

    FeatureFlagMap guest_ini_map;
    if (!options.guest_ini_path.empty()) {
        std::vector<std::string> unknown_guest_features;
        auto guest_ini = ParseFeatureFile(options.guest_ini_path, &unknown_guest_features);
        if (!guest_ini.ok()) {
            return guest_ini.status();
        }
        guest_ini_map = std::move(*guest_ini);
        if (!unknown_guest_features.empty()) {
            LOG(WARNING) << "Unknown feature(s) in " << options.guest_ini_path.string()
                         << "; please update the emulator to one that supports the feature(s): "
                         << absl::StrJoin(unknown_guest_features, ", ");
        }
        if (options.guest_api_level >= 37) {
            // API 37+ system images include Minigbm and GuestAngle support in the guest image
            // even when omitted from the system image's advancedFeatures.ini.
            guest_ini_map.try_emplace(android_studio::EmulatorFeatureFlagState::MINIGBM,
                                      FeatureStatus::On);
            guest_ini_map.try_emplace(android_studio::EmulatorFeatureFlagState::GUEST_ANGLE,
                                      FeatureStatus::On);
        }
    }

    const auto is_enabled_in = [](const FeatureFlagMap& map, EmulatorFeatureFlag feature) {
        const auto it = map.find(feature);
        return it != map.end() && it->second == FeatureStatus::On;
    };
    for (auto& [feature, state] : flags.features_) {
        if (IsGuestFeature(feature)) {
            state.is_enabled_by_guest = is_enabled_in(guest_ini_map, feature);
            state.baseline_val = is_enabled_in(host_ini_map, feature) && state.is_enabled_by_guest;
        } else {
            state.baseline_val = is_enabled_in(host_ini_map, feature);
        }
        state.current_val = state.baseline_val;
    }

    // 1. User INI override (~/.android/advancedFeatures.ini)
    if (android::base::file::exists(options.user_ini_path)) {
        std::vector<std::string> unknown_user_features;
        auto user_ini = ParseFeatureFile(options.user_ini_path, &unknown_user_features);
        if (!user_ini.ok()) {
            return user_ini.status();
        }
        if (!unknown_user_features.empty()) {
            LOG(WARNING) << "Ignoring unknown feature(s) in " << options.user_ini_path.string()
                         << ": " << absl::StrJoin(unknown_user_features, ", ");
        }
        for (const auto& [feature, status] : *user_ini) {
            if (status == FeatureStatus::On) {
                flags.SetEnabledOverride(feature, true);
            } else if (status == FeatureStatus::Off) {
                flags.SetEnabledOverride(feature, false);
            }
        }
    }

    // 2. AVD hardware config override (hw.featureflags)
    flags.ApplyOverrides(options.avd_feature_flags, "hw.featureflags (AVD config.ini)");

    // 3. Environment variable override (ANDROID_EMULATOR_FEATURES)
    flags.ApplyOverrides(options.env_feature_flags, "ANDROID_EMULATOR_FEATURES");

    // 4. Command-line override (-feature)
    for (const std::string& cli_arg : options.cli_feature_flags) {
        flags.ApplyOverrides(cli_arg, "-feature");
    }

    return flags;
}

bool FeatureFlags::IsEnabled(EmulatorFeatureFlag feature) const {
    if (auto it = features_.find(feature); it != features_.end()) {
        const auto& [flag, state] = *it;
        return state.current_val;
    }
    return false;
}

bool FeatureFlags::IsOverridden(EmulatorFeatureFlag feature) const {
    if (auto it = features_.find(feature); it != features_.end()) {
        const auto& [flag, state] = *it;
        return state.is_overridden;
    }
    return false;
}

bool FeatureFlags::IsEnabledByGuest(EmulatorFeatureFlag feature) const {
    if (auto it = features_.find(feature); it != features_.end()) {
        const auto& [flag, state] = *it;
        return state.is_enabled_by_guest;
    }
    return false;
}

void FeatureFlags::SetEnabledOverride(EmulatorFeatureFlag feature, bool enabled) {
    auto it = features_.find(feature);
    if (it == features_.end()) {
        return;
    }
    auto& [flag, state] = *it;
    state.current_val = enabled;
    state.is_overridden = true;
    VLOG(1) << "Feature '" << FeatureToString(flag) << "' (" << static_cast<int>(flag)
            << ") overridden to " << (enabled ? "enabled" : "disabled");
}

void FeatureFlags::SetIfNotOverridden(EmulatorFeatureFlag feature, bool enabled) {
    auto it = features_.find(feature);
    if (it == features_.end()) {
        return;
    }
    auto& [flag, state] = *it;
    if (state.is_overridden) {
        return;
    }
    if (enabled && IsGuestFeature(flag) && !state.is_enabled_by_guest) {
        return;
    }
    state.baseline_val = enabled;
    state.current_val = enabled;
}

void FeatureFlags::ApplyOverrides(std::string_view overrides, std::string_view source) {
    if (overrides.empty()) {
        return;
    }
    for (std::string_view token : absl::StrSplit(overrides, ',', absl::SkipEmpty())) {
        std::string_view item = absl::StripAsciiWhitespace(token);
        if (item.empty()) {
            continue;
        }
        bool enable = true;
        if (item.front() == '-') {
            enable = false;
            item.remove_prefix(1);
            item = absl::StripAsciiWhitespace(item);
        } else if (item.front() == '+') {
            item.remove_prefix(1);
            item = absl::StripAsciiWhitespace(item);
        }
        if (item.empty()) {
            continue;
        }
        auto feature = StringToFeature(item);
        if (!feature.has_value()) {
            LOG(WARNING) << "Unknown feature flag in override"
                         << (source.empty() ? "" : absl::StrCat(" from ", source)) << ": '" << item
                         << "'";
            continue;
        }
        if (IsIgnoredLegacy(*feature)) {
            VLOG(1) << "Ignoring legacy feature flag override: " << item;
            continue;
        }
        SetEnabledOverride(*feature, enable);
    }
}

template <typename Predicate>
std::vector<EmulatorFeatureFlag> FeatureFlags::CollectFeatures(Predicate predicate) const {
    std::vector<EmulatorFeatureFlag> res;
    for (const auto& [feature, state] : features_) {
        if (predicate(state)) {
            res.push_back(feature);
        }
    }
    SortFeatures(res);
    return res;
}

std::vector<EmulatorFeatureFlag> FeatureFlags::GetEnabled() const {
    return CollectFeatures([](const FeatureState& state) { return state.current_val; });
}

std::vector<EmulatorFeatureFlag> FeatureFlags::GetEnabledNonOverride() const {
    return CollectFeatures([](const FeatureState& state) { return state.baseline_val; });
}

std::vector<EmulatorFeatureFlag> FeatureFlags::GetEnabledOverride() const {
    return CollectFeatures(
            [](const FeatureState& state) { return state.is_overridden && state.current_val; });
}

std::vector<EmulatorFeatureFlag> FeatureFlags::GetDisabledOverride() const {
    return CollectFeatures(
            [](const FeatureState& state) { return state.is_overridden && !state.current_val; });
}

}  // namespace android::goldfish
