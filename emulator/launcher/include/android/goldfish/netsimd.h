// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <grpcpp/grpcpp.h>

#include "absl/status/statusor.h"

#include "android/cmdline-option.h"
#include "android/emulation/control/utils/emulator_grpc_client.h"
#include "goldfish/async/launch_config.h"

namespace android::goldfish {

int read_netsim_port();

using NetsimConnection_ptr =
        std::unique_ptr<android::emulation::control::BlockingEmulatorGrpcClient>;

absl::StatusOr<NetsimConnection_ptr> connect_to_netsim(const std::string& endpoint,
                                                       absl::Duration connection_deadline);

absl::StatusOr<::goldfish::async::LaunchConfig> netsimd_launch_config(
        const std::filesystem::path& netsim_binary, const AndroidOptions& opts);

}  // namespace android::goldfish