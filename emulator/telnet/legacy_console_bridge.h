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
#include <filesystem>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "android/status/status_macros.h"
#include "command_registry.h"
#include "emulator_controller.grpc.pb.h"
#include "line_command_handler.h"

namespace goldfish::telnet {

/**
 * @brief A bridge that maps legacy Telnet console commands to modern gRPC
 * service calls.
 *
 * This class reimplements the legacy Telnet console functionality by using
 * gRPC to communicate with the emulator backend. It uses a `CommandRegistry`
 * to define the command hierarchy and dispatch logic.
 */
class LegacyConsoleBridge : public LineCommandHandler {
  public:
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

        virtual absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext(
                std::chrono::time_point<std::chrono::system_clock> deadline =
                        std::chrono::system_clock::now() + std::chrono::milliseconds(500)) {
            ASSIGN_OR_RETURN(auto client, Client());
            ASSIGN_OR_RETURN(auto context, client->NewContext());
            context->set_deadline(deadline);
            return context;
        }

      private:
        int port_;
        std::shared_ptr<android::emulation::control::BlockingEmulatorGrpcClient> client_
                ABSL_GUARDED_BY(mutex_);
        absl::Mutex mutex_;
    };

    /**
     * @brief Constructs the bridge with a gRPC client and the path to the auth
     * token.
     *
     * @param client The gRPC client used to make calls to the emulator services.
     * @param token_path Path to the file containing the console authentication
     * token.
     */
    LegacyConsoleBridge(int port, std::filesystem::path token_path);

    // LineCommandHandler implementation
    absl::StatusOr<std::string> operator()(std::string line, Context& ctx) override;
    std::string WelcomeMessage(const Context& ctx) const override;

    std::unique_ptr<Context> CreateContext() const override {
        return std::make_unique<ConsoleContext>(port_);
    };

  private:
    int port_;
    std::filesystem::path token_path_;
    std::unique_ptr<CommandRegistry> registry_;
};

}  // namespace goldfish::telnet
