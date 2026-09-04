// Copyright (C) 2026 The Android Open Source Project
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
#include "console_context.h"

#include <grpcpp/grpcpp.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/goldfish/ini_file.h"
#include "android/status/status_macros.h"
#include "goldfish/discovery/emulator_advertisement.h"
#include "netsim/cell.grpc.pb.h"
#include "netsim/common.pb.h"
#include "netsim/frontend.grpc.pb.h"
#include "netsim_connection_provider.h"

namespace goldfish::telnet {

using android::emulation::control::GrpcStatusToAbslStatus;
using android::goldfish::IniFile;

absl::StatusOr<std::unique_ptr<::netsim::cell::CellService::StubInterface>>
ConsoleContext::NetsimCellStub() {
    ASSIGN_OR_RETURN(auto client, NetsimConnectionProvider::GetClient());
    return client->Stub<::netsim::cell::CellService>();
}

absl::StatusOr<std::unique_ptr<::netsim::frontend::FrontendService::StubInterface>>
ConsoleContext::NetsimFrontendStub() {
    ASSIGN_OR_RETURN(auto client, NetsimConnectionProvider::GetClient());
    return client->Stub<::netsim::frontend::FrontendService>();
}

absl::StatusOr<uint32_t> ConsoleContext::GetCellularChipId() {
    {
        absl::MutexLock lock(&mutex_);
        if (cellular_chip_id_.has_value()) {
            return *cellular_chip_id_;
        }
    }

    ASSIGN_OR_RETURN(auto stub, NetsimFrontendStub());
    ASSIGN_OR_RETURN(auto context, NewNetsimContext());
    google::protobuf::Empty request;
    ::netsim::frontend::ListDeviceResponse response;

    auto status = stub->ListDevice(context.get(), request, &response);
    RETURN_IF_ERROR(GrpcStatusToAbslStatus(status));

    std::string target_name = NetsimConnectionProvider::GetAvdName();
    for (const auto& device : response.devices()) {
        if (device.name() == target_name) {
            for (const auto& chip : device.chips()) {
                if (chip.kind() == ::netsim::common::ChipKind::CELLULAR) {
                    absl::MutexLock lock(&mutex_);
                    cellular_chip_id_ = chip.id();
                    return chip.id();
                }
            }
        }
    }

    return absl::NotFoundError(
            absl::StrCat("No cellular chip found in netsim for AVD: ", target_name));
}

absl::StatusOr<std::unique_ptr<grpc::ClientContext>> ConsoleContext::NewNetsimContext(
        std::chrono::time_point<std::chrono::system_clock> deadline) {
    ASSIGN_OR_RETURN(auto client, NetsimConnectionProvider::GetClient());
    ASSIGN_OR_RETURN(auto context, client->NewContext());
    context->set_deadline(deadline);
    return context;
}

absl::StatusOr<std::shared_ptr<android::emulation::control::BlockingEmulatorGrpcClient>>
ConsoleContext::Client() {
    absl::MutexLock lock(&mutex_);
    if (client_) {
        return client_;
    }

    auto client = android::emulation::control::EmulatorGrpcClientBuilder()
                          .ForDiscoveredEmulator({{"port.serial", std::to_string(port_)}})
                          .BuildBlocking();
    if (!client.ok()) {
        LOG(ERROR) << "Failed to build gRPC client: " << client.status();
        return client.status();
    }

    VLOG(1) << "Connecting to gRPC server...";
    if (auto s = (*client)->Connect(absl::Seconds(2)); !s.ok()) {
        LOG(ERROR) << "Failed to connect to gRPC server: " << s;
        return s;
    }

    VLOG(1) << "Successfully connected to gRPC server.";
    client_ = std::move(*client);
    return client_;
}

absl::StatusOr<std::vector<std::filesystem::path>> ConsoleContext::DiscoverRunningEmulators() {
    return discovery::EmulatorAdvertisement().DiscoverRunningEmulators();
}

absl::StatusOr<DiscoveredEmulator> ConsoleContext::DiscoverEmulatorWithProperties(
        const absl::flat_hash_map<std::string, std::string>& props) {
    auto discovered = DiscoverRunningEmulators();
    if (!discovered.ok()) return discovered.status();

    absl::StatusOr<DiscoveredEmulator> result = absl::NotFoundError("No matching emulator found");
    for (const auto& discovery_file : *discovered) {
        IniFile ini(discovery_file);
        if (!ini.Read()) continue;

        bool match = true;
        for (const auto& [key, val] : props) {
            match = match && ini.HasKey(key) && ini.GetString(key) == val;
        }
        if (match) {
            if (result.ok()) {
                return absl::FailedPreconditionError("Multiple matching emulators found");
            }
            DiscoveredEmulator candidate;
            candidate.discovery_file = discovery_file;
            for (const auto& entry : ini) {
                candidate.properties[entry.first] = entry.second;
            }
            result = std::move(candidate);
        }
    }
    return result;
}

}  // namespace goldfish::telnet
