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
#include "grpc_device.h"

#include <filesystem>
#include <initializer_list>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "android/base/bazel/bazel_info.h"
#include "android/base/system/System.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"

using android::base::Bazel;
using android::base::System;
namespace fs = std::filesystem;

namespace android::goldfish {
absl::Status GrpcDevice::initialize(const Emulator& emulator) {
    return absl::OkStatus();
}

// TODO(jansene) add kernel versioning magic to add/subtract parameters,
std::vector<std::string> GrpcDevice::getQemuParameters(const Emulator& emulator) const {
    fs::path allowlist = System::get()->getLauncherDirectory() / "lib" / "emulator_access.json";

    if (Bazel::inBazel()) {
        // Development environment, allow access to the emulator.
        allowlist = fs::path(Bazel::runfilesPath(
                "_main/hardware/generic/goldfish/emulator/grpc/security/test/"
                "android/emulation/control/secure/test_allow_list.json"));
        assert(fs::exists(allowlist));
        LOG(WARNING) << "** Using development allow list, do not use in production **";
    }

    std::string grpc_device = absl::StrFormat("grpc,port=%d,token=true,allowlist=%s", 8556,
                                              System::pathAsString(allowlist));

    return {"-device", grpc_device, "-trace", "module_*"};
}

}  // namespace android::goldfish