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
#pragma once

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "android/status/status_macros.h"
#include "emulator_controller.grpc.pb.h"
#include "line_command_handler.h"
#include "modem_service.grpc.pb.h"
#include "netsim/cell.grpc.pb.h"
#include "netsim/frontend.grpc.pb.h"
#include "screen_recording_service.grpc.pb.h"
#include "snapshot_service.grpc.pb.h"

namespace goldfish::telnet {

struct DiscoveredEmulator {
    std::filesystem::path discovery_file;
    absl::flat_hash_map<std::string, std::string> properties;
};

/**
 * @brief Context for legacy console command handlers.
 */
struct ConsoleContext : public LineCommandHandler::Context {
    explicit ConsoleContext(int port) : port_(port) {}

    absl::StatusOr<std::shared_ptr<android::emulation::control::BlockingEmulatorGrpcClient>>
    Client();

    int Port() const { return port_; }

    virtual absl::StatusOr<
            std::unique_ptr<android::emulation::control::EmulatorController::StubInterface>>
    EmulatorControllerStub() {
        ASSIGN_OR_RETURN(auto client, Client());
        return client->Stub<android::emulation::control::EmulatorController>();
    }

    virtual absl::StatusOr<std::unique_ptr<
            android::emulation::control::incubating::ScreenRecording::StubInterface>>
    ScreenRecordingStub() {
        ASSIGN_OR_RETURN(auto client, Client());
        return client->Stub<android::emulation::control::incubating::ScreenRecording>();
    }

    virtual absl::StatusOr<
            std::unique_ptr<android::emulation::control::incubating::Modem::StubInterface>>
    ModemStub() {
        ASSIGN_OR_RETURN(auto client, Client());
        return client->Stub<android::emulation::control::incubating::Modem>();
    }

    virtual absl::StatusOr<std::unique_ptr<::netsim::cell::CellService::StubInterface>>
    NetsimCellStub();

    virtual absl::StatusOr<std::unique_ptr<::netsim::frontend::FrontendService::StubInterface>>
    NetsimFrontendStub();

    virtual absl::StatusOr<uint32_t> GetCellularChipId();

    static constexpr std::chrono::milliseconds kDefaultGrpcDeadline =
            std::chrono::milliseconds(500);

    virtual absl::StatusOr<
            std::unique_ptr<android::emulation::control::SnapshotService::StubInterface>>
    SnapshotStub() {
        ASSIGN_OR_RETURN(auto client, Client());
        return client->Stub<android::emulation::control::SnapshotService>();
    }

    virtual absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + kDefaultGrpcDeadline) {
        ASSIGN_OR_RETURN(auto client, Client());
        ASSIGN_OR_RETURN(auto context, client->NewContext());
        context->set_deadline(deadline);
        return context;
    }

    virtual absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewNetsimContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + kDefaultGrpcDeadline);

    virtual absl::StatusOr<std::vector<std::filesystem::path>> DiscoverRunningEmulators();

    virtual absl::StatusOr<DiscoveredEmulator> DiscoverEmulatorWithProperties(
            const absl::flat_hash_map<std::string, std::string>& props);

    // Triggers an immediate termination of the emulator instance. Note that
    // TERMINATE force-kills the process without generating a Crashpad minidump.
    virtual void TriggerCrash() {
        if (auto stub = EmulatorControllerStub(); stub.ok()) {
            std::thread([stub = *std::move(stub)]() mutable {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                auto context = std::make_unique<grpc::ClientContext>();
                android::emulation::control::VmRunState request;
                request.set_state(android::emulation::control::VmRunState::TERMINATE);
                google::protobuf::Empty response;
                (void)stub->setVmState(context.get(), request, &response);
            }).detach();
        }
    }

  private:
    int port_;
    std::shared_ptr<android::emulation::control::BlockingEmulatorGrpcClient> client_
            ABSL_GUARDED_BY(mutex_);
    std::optional<uint32_t> cellular_chip_id_ ABSL_GUARDED_BY(mutex_);
    absl::Mutex mutex_;
};

}  // namespace goldfish::telnet
