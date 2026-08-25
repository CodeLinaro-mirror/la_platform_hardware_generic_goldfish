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

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include "android/cmdline_option.h"
#include "android/emulation/control/emulator_grpc_client.h"
#include "goldfish/async/launch_config.h"

namespace android::goldfish::netsim {

using NetsimConnection_ptr =
        std::unique_ptr<android::emulation::control::BlockingEmulatorGrpcClient>;

absl::StatusOr<::goldfish::async::LaunchConfig> netsimd_launch_config(
        const std::filesystem::path& netsim_binary, const AndroidOptions& opts);

class NetsimConnector {
  public:
    using LaunchNetsimdFn = std::function<absl::Status()>;
    using ConnectFn = std::function<absl::StatusOr<NetsimConnection_ptr>(
            const std::string& endpoint, absl::Duration deadline)>;
    using PortReaderFn = std::function<int()>;

    NetsimConnector(LaunchNetsimdFn launch_netsimd_on_loop, const std::atomic<bool>& shutting_down,
                    std::optional<std::string> force_existing_netsimd_endpoint = std::nullopt,
                    ConnectFn connect_fn = ConnectToNetsim,
                    PortReaderFn port_reader_fn = ReadNetsimPort);

    absl::StatusOr<NetsimConnection_ptr> Run();

  private:
    static int ReadNetsimPort();
    static absl::StatusOr<NetsimConnection_ptr> ConnectToNetsim(const std::string& endpoint,
                                                                absl::Duration connection_deadline);

    absl::StatusOr<int> WaitForNewPort(int stale_port, absl::Duration timeout);

    LaunchNetsimdFn launch_netsimd_fn_;
    const std::atomic<bool>& shutting_down_;
    std::optional<std::string> force_existing_netsimd_endpoint_;
    ConnectFn connect_fn_;
    PortReaderFn get_netsimd_port_fn_;
};

}  // namespace android::goldfish::netsim