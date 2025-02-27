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

#include <initializer_list>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"

#include "android/base/bazel/bazel_info.h"
#include "android/base/system/System.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"

namespace android::goldfish {

using android::base::Bazel;

static std::string qemu_exe(const Avd& avd) {
    const bool inBazel = Bazel::inBazel();
    std::string baseName;

#ifdef __APPLE__
    constexpr std::string_view bazelPostfix = "_signed";
#else
    constexpr std::string_view bazelPostfix = "_std";
#endif

    switch (avd.detectArchitecture()) {
        case Avd::CpuArchitecture::kArm:
            baseName = "qemu-system-aarch64";
            break;
        case Avd::CpuArchitecture::kX86:
            baseName = "qemu-system-x86_64";
            break;
        case Avd::CpuArchitecture::kRiscV:
            baseName = "qemu-system-riscv64";
            break;
        default:
            return "unknown";
    }

    return inBazel ? absl::StrCat(baseName, bazelPostfix) : baseName;
}

absl::Status Machine::initialize(const Emulator& emulator) {
    const Avd& avd = emulator.avd();
    auto qemu = qemu_exe(avd);
    mBinary = base::System::findBundledExecutable(qemu);

    if (mBinary.empty()) {
        return absl::NotFoundError(absl::StrCat("Could not find the qemu binary: ", qemu));
    }
    return absl::OkStatus();
}

// TODO(jansene) add kernel versioning magic to add/subtract parameters,
std::vector<std::string> Machine::getQemuParameters(const Emulator& emulator) const {
    return {"-machine",
            "goldfish,vendor=/dev/block/pci/pci0000:00/0000:00:07.0/by-name/"
            "vendor,system=/dev/block/pci/pci0000:00/0000:00:03.0/by-name/system"};
}

}  // namespace android::goldfish
