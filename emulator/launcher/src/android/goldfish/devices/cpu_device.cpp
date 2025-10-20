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

#include <initializer_list>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/cpu/CpuAccelerator.h"

namespace android::goldfish {

absl::Status CpuDevice::initialize(const EmulatorConfig& emulator) {
    // TODO(jansene): do a series of checks.
    // TODO(invoking qemu --accel help will give supported hypervisors)
    // TODO(invoking qemu --cpu help will give supported cpus)

    mCores = emulator.avd().hw().hw_cpu_ncore;
    if (auto *c = emulator.opts().cores; c != nullptr) {
        uint64_t cores;
        if (absl::SimpleAtoi(c, &cores)) {
            mCores = cores;
        } else {
            return absl::InvalidArgumentError(absl::StrCat("Failed to parse -cores flag: ", c));
        }
    }

    return absl::OkStatus();
}

namespace {
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
}  // namespace

// static
void CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture arch) {
    the_forced_arch = arch;
}

std::vector<std::string> CpuDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    const Avd& avd = emulator.avd();
    auto hw = avd.hw();


    std::vector<std::string> params;
    params.insert(params.end(), {"-smp", std::to_string(mCores)});

    auto target_arch = avd.detectArchitecture();
    {
        std::string cpu = "host";
        switch (target_arch) {
            case Avd::CpuArchitecture::kArm:
                cpu = "cortex-a53";
                break;
            case Avd::CpuArchitecture::kX86:
                // TODO: switch to better cpu model for linux/windows
                cpu = "SandyBridge";
                break;
            case Avd::CpuArchitecture::kRiscV:
            case Avd::CpuArchitecture::kUnknown:
            default:
                break;
        }
        params.insert(params.end(), {"-cpu", cpu});
    }

    {
        auto supported = GetCurrentCpuAccelerator();
        if (emulator.opts().no_accel) {
            LOG(WARNING) << "-no-accel option passed so forcing TCG. This will "
                         << "result in a very slow emulator!";
            supported = CPU_ACCELERATOR_NONE;
        }
        if (char* accel = emulator.opts().accel; accel && std::string(accel) == "off") {
            LOG(WARNING) << "-accel off option passed so forcing TCG. This will "
                         << "result in a very slow emulator!";
            supported = CPU_ACCELERATOR_NONE;
        }
        // TODO(whollins): support any other values of -accel flag?
        if (target_arch != getHostArch()) {
            LOG(WARNING) << "target arch does not match host arch so forcing TCG. "
                         << "This will result in a very slow emulator!";
            supported = CPU_ACCELERATOR_NONE;
        }

        std::string accel = "tcg";
        if (supported != CPU_ACCELERATOR_NONE) {
            accel = CpuAcceleratorToString(supported);
        } else {
            LOG(WARNING) << "Using TCG, which is not going to be fast!";
        }
        params.insert(params.end(), {"-accel", accel});
    }

    return params;
}

}  // namespace android::goldfish
