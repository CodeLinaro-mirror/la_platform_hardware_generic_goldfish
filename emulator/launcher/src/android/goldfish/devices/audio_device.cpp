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
#include "audio_device.h"

#include <initializer_list>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"
namespace android::goldfish {
absl::Status AudioDevice::initialize(const Emulator& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> AudioDevice::getQemuParameters(const Emulator& emulator) const {
    using namespace std::literals;

    const std::string_view audioDriver = "none"sv;

    switch (emulator.avd().detectArchitecture()) {
    case Avd::CpuArchitecture::kArm:
        return {
            "-audiodev"s,
            absl::StrCat(audioDriver, ",id=mainaudiodev,out.mixing-engine=off"sv),
            "-device"s,
            "virtio-sound-device,audiodev=mainaudiodev"s,
        };

    case Avd::CpuArchitecture::kX86:
        return {
            "-audiodev"s,
            absl::StrCat(audioDriver, ",id=mainaudiodev,out.mixing-engine=off"sv),
            "-device"s,
            absl::StrCat("virtio-sound-pci,audiodev=mainaudiodev,addr="sv, addr()),
        };

    case Avd::CpuArchitecture::kRiscV:
    case Avd::CpuArchitecture::kUnknown:
        break;
    }

    return {};
}

}  // namespace android::goldfish
