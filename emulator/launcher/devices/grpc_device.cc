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

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "goldfish/file/file.h"

namespace android::goldfish {

absl::Status GrpcDevice::initialize(const EmulatorConfig& emulator) {
    mPort = emulator.serial_number() + 3000;
    if (char* grpc = emulator.opts().grpc) {
        if (int grpcPort; absl::SimpleAtoi(grpc, &grpcPort)) {
            mPort = grpcPort;
        } else {
            LOG(WARNING) << "Failed to parse grpc port number: '" << grpc
                         << "'. Using default port: " << mPort;
        }
    }
    if (char* allowlist_str = emulator.opts().grpc_allowlist) {
        if (base::file::exists(allowlist_str)) {
            mAllowlist.assign(allowlist_str);
        } else {
            LOG(WARNING) << "grpc_allowlist file does not exist: '" << mAllowlist
                         << "'. Using default";
        }
    }

    return absl::OkStatus();
}

std::vector<std::string> GrpcDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    fs::path allowlist = mAllowlist;
    if (allowlist.empty()) {
        allowlist = emulator.paths().launcher_directory / "lib" / "emulator_access.json";
        if (!base::file::exists(allowlist)) {
            LOG(WARNING) << "GRPC default allowlist doesn't exist, this may cause problems";
        }
    }

    std::vector<std::pair<std::string, std::string>> params{
        {"port", absl::StrCat(mPort)},
        {"token", "true"},
        {"allowlist", allowlist.string()},
        {"discovery_dir", emulator.paths().discovery_directory.string()},
        {"launcher_dir", emulator.paths().launcher_directory.string()}};
    params.emplace_back(std::pair{"logging", emulator.opts().verbose_grpc ? "true" : "false"});
    params.emplace_back(std::pair{"embedded", emulator.opts().qt_hide_window ? "true" : "false"});

    auto grpc_params = absl::StrJoin(params, ",", [](std::string* s, const auto& pair) {
        absl::StrAppend(s, pair.first, "=", pair.second);
    });

    return {"-device", absl::StrCat("grpc,", grpc_params)};
}

}  // namespace android::goldfish
