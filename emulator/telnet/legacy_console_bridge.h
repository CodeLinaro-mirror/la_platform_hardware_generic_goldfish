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

#include <filesystem>
#include <memory>
#include <string>

#include "absl/status/statusor.h"

#include "command_registry.h"
#include "console_context.h"
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
    using DiscoveredEmulator = goldfish::telnet::DiscoveredEmulator;
    using ConsoleContext = goldfish::telnet::ConsoleContext;

    /**
     * @brief Constructs the bridge with a gRPC client and the path to the auth
     * token.
     *
     * @param port The port number of the emulator instance.
     * @param token_path Path to the file containing the console authentication
     * token.
     */
    LegacyConsoleBridge(int port, std::filesystem::path token_path);

    // LineCommandHandler implementation
    absl::StatusOr<std::string> operator()(std::string line, Context& ctx) override;
    std::string WelcomeMessage(const Context& ctx) const override;

    std::unique_ptr<Context> CreateContext() const override;

  private:
    int port_;
    std::filesystem::path token_path_;
    std::unique_ptr<CommandRegistry> registry_;
};

}  // namespace goldfish::telnet
