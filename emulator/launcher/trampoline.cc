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

#include "trampoline.h"

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"

#include "android/base/system.h"
#include "android/goldfish/feature_flags.h"
#include "android/process/command.h"
#include "goldfish/file/file.h"

namespace android::goldfish {
namespace {

constexpr std::string_view kNoTrampolineEnvVar = "AEMU_NO_TRAMPOLINE";

bool LastRunQemuVersion2(const android::goldfish::Avd& avd) {
    return avd.GetLastRunQemuVersion().value_or(0) == 2;
}

bool HasMustHaveGuestFeatures(const android::goldfish::Avd& avd) {
    // Emu Next does't support system images that don't have these features available.
    constexpr std::array kMustHaveFeatures{
        android_studio::EmulatorFeatureFlagState::MAC80211HWSIM_USERSPACE_MANAGED,
    };

    auto sys_img_features =
            android::goldfish::ParseFeatureFile(avd.GetSystemImagePaths().advanced_features);
    if (!sys_img_features.ok()) {
        LOG(ERROR) << "Failed to parse system image features file: " << sys_img_features.status();
        return false;
    }

    for (auto must_have : kMustHaveFeatures) {
        if (sys_img_features->find(must_have) == sys_img_features->end()) {
            VLOG(1) << "Sysimg is missing must have feature: "
                    << android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_Name(
                               must_have);
            return false;
        }
    }

    return true;
}

bool DefinitelyNotPhone(const android::goldfish::Avd& avd) {
    auto device_type = avd.GetDeviceType();
    return device_type != android::goldfish::DeviceType::kPhone &&
           device_type != android::goldfish::DeviceType::kUnknown;
}

}  // namespace

bool ShouldTrampolineToQemu2(const android::goldfish::Avd& avd) {
    if (!android::base::System::Get()->EnvGet(kNoTrampolineEnvVar).empty()) {
        VLOG(1) << "Not trampolining as AEMU_NO_TRAMPOLINE is set";
        return false;
    }

    if (auto forced_version = avd.ForcedTrampolineVersion(); forced_version == 2) {
        VLOG(1) << "Trampolining as force trampoline qemu version is 2";
        return true;
    } else if (forced_version >= 10) {
        VLOG(1) << "Not trampolining as force trampoline qemu version is 10 or greater";
        return false;
    }

    if (!HasMustHaveGuestFeatures(avd)) {
        VLOG(1) << "Trampolining as sysimg is missing required feature";
        return true;
    }

    if (DefinitelyNotPhone(avd)) {
        VLOG(1) << "Trampolining as device type is not phone";
        return true;
    }

    if (avd.ApiLevel() < 37) {
        VLOG(1) << "Trampolining as api level is less than 37";
        return true;
    }

    if (LastRunQemuVersion2(avd)) {
        LOG(WARNING)
                << "This AVD was last used with the legacy emulator so will now try to switch. Use "
                   "-wipe-data option to reset the AVD data and use this emulator.";
        VLOG(1) << "Trampolining as last run qemu version is 2";
        return true;
    }

    VLOG(1) << "Not trampolining as no other conditions are met";
    return false;
}

[[noreturn]] void TrampolineToQemu2(const fs::path& launcher_directory,
                                    std::vector<std::string> args) {
#ifdef _WIN32
    constexpr std::string_view emulator_binary = "emulator.exe";
#else
    constexpr std::string_view emulator_binary = "emulator";
#endif
    const fs::path qemu2_binary_path =
            launcher_directory.parent_path().parent_path() / "emulator" / emulator_binary;
    LOG(WARNING) << "Trampolining to legacy emulator: " << qemu2_binary_path.string();
    if (!android::base::file::exists(qemu2_binary_path)) {
        LOG(FATAL) << "Trying to trampoline but legacy emulator binary does not exist: "
                   << qemu2_binary_path.string();
    }
    if (!android::base::file::can_exec(qemu2_binary_path)) {
        LOG(FATAL) << "Trying to trampoline but cannot execute legacy emulator binary: "
                   << qemu2_binary_path.string();
    }

    std::vector<std::string> qemu2_args;
    qemu2_args.reserve(args.size() + 1);

    qemu2_args.push_back(qemu2_binary_path.string());
    qemu2_args.insert(qemu2_args.end(), args.begin(), args.end());
    android::base::System::Get()->EnvSet(std::string(kNoTrampolineEnvVar), "1");
    android::base::Command::Create(qemu2_args).Replace().Execute();
    LOG(FATAL) << "Trampoline to legacy emulator failed for unknown reason";
}

}  // namespace android::goldfish
