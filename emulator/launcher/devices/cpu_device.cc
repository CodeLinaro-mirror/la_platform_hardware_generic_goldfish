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

#include "cpu_device.h"

#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

#include "aemu/base/utils/status_macros.h"
#include "android/cpu/cpu_accelerator.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/emulator_config.h"
#include "android/goldfish/hardware_config.h"

namespace android::goldfish {

namespace {

using namespace std::string_view_literals;

Avd::CpuArchitecture the_forced_arch = Avd::CpuArchitecture::kUnknown;

Avd::CpuArchitecture getHostArch() {
    if (the_forced_arch != Avd::CpuArchitecture::kUnknown) {
        return the_forced_arch;
    }

#if defined(__arm64__)
    return Avd::CpuArchitecture::kArm;
#elif defined(__x86_64__)
    return Avd::CpuArchitecture::kX86;
#else
    return Avd::CpuArchitecture::kUnknown;
#endif
}

bool getAccelForcedOff(const EmulatorConfig& emulator) {
    if (char* accel = emulator.opts().accel; accel && std::string_view(accel) == "off"sv) {
        return true;
    }
    return emulator.opts().no_accel;
}

absl::StatusOr<std::string> getAccelString(const EmulatorConfig& emulator,
                                           Avd::CpuArchitecture host_arch,
                                           Avd::CpuArchitecture target_arch) {
    if (getAccelForcedOff(emulator)) {
        LOG(WARNING) << "-no-accel option passed so forcing TCG. This will "
                     << "result in a very slow emulator!";
        return "tcg";
    }

    auto supported = GetCurrentCpuAccelerator();
    if (supported == CPU_ACCELERATOR_NONE) {
        return absl::InvalidArgumentError(
                "CPU accelerator not available. If you really want to use TCG then pass -no-accel "
                "option");
    }

    if (target_arch != host_arch) {
        LOG(WARNING) << "target arch does not match host arch so forcing TCG. "
                     << "This will result in a very slow emulator!";
        return absl::InvalidArgumentError(
                "CPU accelerator does not match target arch. If you really want to use TCG then "
                "pass -no-accel option");
    }

    // TODO(whollins): support any other values of -accel flag?

    return CpuAcceleratorToString(supported);
}

absl::StatusOr<std::string> getCpuString(Avd::CpuArchitecture target_arch) {
    switch (target_arch) {
    case Avd::CpuArchitecture::kArm:
        return "cortex-a53";
    case Avd::CpuArchitecture::kX86:
        // TODO(hshan): switch to better cpu model for linux/windows
        // Maybe "host"?
        return "SandyBridge";
    case Avd::CpuArchitecture::kRiscV:
    case Avd::CpuArchitecture::kUnknown:
    default:
        return absl::UnimplementedError("No CPU available for target architecture");
    }
}

absl::StatusOr<int> getCores(const EmulatorConfig& emulator, const HardwareConfig& hw) {
    if (auto* c = emulator.opts().cores; c != nullptr) {
        uint64_t cores;
        if (absl::SimpleAtoi(c, &cores)) {
            return cores;
        } else {
            return absl::InvalidArgumentError(absl::StrCat("Failed to parse -cores flag: ", c));
        }
    }
    return hw.hw_cpu_ncore;
}

}  // namespace

// static
void CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture arch) {
    the_forced_arch = arch;
}

absl::Status CpuDevice::initialize(const EmulatorConfig& emulator) {
    // TODO(jansene): do a series of checks.
    // TODO(invoking qemu --accel help will give supported hypervisors)
    // TODO(invoking qemu --cpu help will give supported cpus)

    const Avd& avd = emulator.avd();
    auto target_arch = avd.DetectArchitecture();

    ASSIGN_OR_RETURN(mAccelerator, getAccelString(emulator, getHostArch(), target_arch));
    ASSIGN_OR_RETURN(mCpu, getCpuString(target_arch));
    ASSIGN_OR_RETURN(mCores, getCores(emulator, avd.Hw()));

    return absl::OkStatus();
}

std::vector<std::string> CpuDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    return {
        "-smp", std::to_string(mCores), "-cpu", mCpu, "-accel", mAccelerator,
    };
}

}  // namespace android::goldfish
