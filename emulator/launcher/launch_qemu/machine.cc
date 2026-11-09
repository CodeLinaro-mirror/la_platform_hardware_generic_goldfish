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

#include "machine.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/goldfish/avd.h"
#include "android/goldfish/constants.h"
#include "android/goldfish/hardware_config.h"

namespace android::goldfish {

namespace {
absl::StatusOr<std::string> machine(const EmulatorConfig& emulator) {
    const Avd& avd = emulator.avd();
    std::string machine_arg;

    const auto arch = avd.Arch();
    switch (arch) {
    case Avd::CpuArchitecture::kArm:
        machine_arg = "goldfish-arm";
        break;
    case Avd::CpuArchitecture::kX86:
        machine_arg = "goldfish";
        break;
    default:
        return absl::UnimplementedError(absl::StrCat("Machine type not supported: ", arch));
    }

    const int api_level = avd.ApiLevel();
    if (api_level < kVirtioReadyApiLevel) {
        absl::StrAppend(&machine_arg, ",goldfish-pipe=on");
        absl::StrAppend(&machine_arg, ",goldfish-sync=on");
    }
    if (api_level > 0 && api_level < kVirtioInputMinApiLevel) {
        absl::StrAppend(&machine_arg, ",goldfish-events=on");
    }

    using S = android_studio::EmulatorFeatureFlagState;
    const auto& ff = emulator.feature_flags();
    if (arch == Avd::CpuArchitecture::kX86 && api_level > 0 && api_level < 29 &&
        !ff.IsEnabled(S::DYNAMIC_PARTITION)) {
        if (!avd.GetSystemImagePaths().vendor_image.empty()) {
            absl::StrAppend(&machine_arg,
                            ",vendor=/dev/block/pci/pci0000:00/0000:00:07.0/by-name/vendor");
        }
        if (!ff.IsEnabled(S::SYSTEM_AS_ROOT) && api_level >= 26) {
            absl::StrAppend(&machine_arg,
                            ",system=/dev/block/pci/pci0000:00/0000:00:03.0/by-name/system");
        }
    }

    return machine_arg;
}
}  // namespace

absl::Status Machine::initialize(const EmulatorConfig& emulator) {
    if (auto m = machine(emulator); m.ok()) {
        mMachine = *m;
        return absl::OkStatus();
    } else {
        return m.status();
    }
}

// TODO(jansene) add kernel versioning magic to add/subtract parameters,
std::vector<std::string> Machine::getQemuParameters(const EmulatorConfig& emulator) const {
    return {"-machine", mMachine};
}

}  // namespace android::goldfish
