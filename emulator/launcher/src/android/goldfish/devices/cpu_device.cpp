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
#include "android/goldfish/devices/cpu_device.h"
#include "absl/status/status.h"
#include "aemu/base/Log.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/cpu/CpuAccelerator.h"
#include "android/goldfish/devices/device.h"
#include "android/goldfish/config/emulator.h"

#include <initializer_list>
#include <string_view>

namespace android::goldfish {
absl::Status CpuDevice::initialize(const Emulator& emulator) {
  // TODO(jansene): do a series of checks.
  // TODO(invoking qemu --accel help will give supported hypervisors)
  // TODO(invoking qemu --cpu help will give supported cpus)

  return absl::OkStatus();
}

std::vector<std::string>
CpuDevice::getQemuParameters(const Emulator& emulator) const {
  const Avd& avd = emulator.avd();
  auto hw = avd.hw();

  auto supported = GetCurrentCpuAccelerator();
  auto aarch = avd.detectArchitecture();

  std::string accel = "tcg";
  std::string cpu = "host";
#ifdef __arm64__

  if (aarch == Avd::CpuArchitecture::kArm &&
      supported != CPU_ACCELERATOR_NONE) {
    accel = CpuAcceleratorToString(supported);
    cpu = "cortex-a57";
  } else {
    dwarning("Using TCG, which is not going to be fast!");
    cpu = "Snowridge";
  }
#else
  if (aarch == Avd::CpuArchitecture::kX86 && supported != CPU_ACCELERATOR_NONE) {
    accel = CpuAcceleratorToString(supported);
    cpu = "Snowridge";
  } else {
    dwarning("Using TCG, which is not going to be fast!");
    cpu = "cortex-a57";
  }
#endif
  return {"-smp", std::to_string(hw.hw_cpu_ncore), "-accel", accel, "-cpu",
          cpu};
}

} // namespace android::goldfish