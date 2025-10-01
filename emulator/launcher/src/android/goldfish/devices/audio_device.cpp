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
#include "absl/strings/str_format.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"

namespace android::goldfish {

absl::Status AudioDevice::initialize(const Emulator& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> AudioDevice::getQemuParameters(const Emulator& emulator) const {
    using namespace std::literals;
    const std::string_view kID = "id=mainaudiodev"sv;

    std::string audioBackend = getAudioBackend(emulator.opts());
    std::string_view audioSettings;
    if (audioBackend.empty()) {
        audioBackend = "none"s;
        audioSettings = "out.mixing-engine=off,in.mixing-engine=off"sv;
    } else {
        audioSettings =
                "out.mixing-engine=on,out.fixed-settings=on,"
                "out.frequency=48000,out.format=s16,out.channels=2,"
                "in.mixing-engine=on,in.fixed-settings=on,"
                "in.frequency=48000,in.format=s16,in.channels=1"sv;
    }

    switch (emulator.avd().detectArchitecture()) {
    case Avd::CpuArchitecture::kArm:
        return {
            "-audiodev"s,
            absl::StrFormat("%s,%s,%s", audioBackend, kID, audioSettings),
            "-device"s,
            "virtio-sound-device,audiodev=mainaudiodev"s,
        };

    case Avd::CpuArchitecture::kX86:
        return {
            "-audiodev"s,
            absl::StrFormat("%s,%s,%s", audioBackend, kID, audioSettings),
            "-device"s,
            absl::StrCat("virtio-sound-pci,audiodev=mainaudiodev,addr="sv, addr()),
        };

    case Avd::CpuArchitecture::kRiscV:
    case Avd::CpuArchitecture::kUnknown:
        break;
    }

    return {};
}

std::string AudioDevice::getAudioBackend(const AndroidOptions& opts) {
    if (opts.noaudio) {
        return {};
    }

    const char* const audioBackendOpt = opts.audio;
    if (audioBackendOpt && *audioBackendOpt) {
        return audioBackendOpt;
    } else {
        return detectHostAudioBackend();
    }
}

std::string AudioDevice::detectHostAudioBackend() {
    return {};  // TODO b/448177089
}

}  // namespace android::goldfish
