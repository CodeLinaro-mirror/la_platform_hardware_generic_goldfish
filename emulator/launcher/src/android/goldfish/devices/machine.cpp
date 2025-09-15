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

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"

namespace android::goldfish {

namespace {
absl::StatusOr<std::string> machine(const Avd& avd) {
    switch (auto a = avd.detectArchitecture(); a) {
        case Avd::CpuArchitecture::kArm: {
            return "goldfish-arm";
        }
        case Avd::CpuArchitecture::kX86:
            return "goldfish";
        case Avd::CpuArchitecture::kRiscV:
        default:
            return absl::UnimplementedError(absl::StrCat("Machine type not supported: ", a));
    }
}
}  // namespace

absl::Status Machine::initialize(const Emulator& emulator) {
    const Avd& avd = emulator.avd();
    if (auto m = machine(avd); m.ok()) {
        mMachine = *m;
        return absl::OkStatus();
    } else {
        return m.status();
    }
}

// TODO(jansene) add kernel versioning magic to add/subtract parameters,
std::vector<std::string> Machine::getQemuParameters(const Emulator& emulator) const {
    return {"-machine", mMachine};
}

}  // namespace android::goldfish
