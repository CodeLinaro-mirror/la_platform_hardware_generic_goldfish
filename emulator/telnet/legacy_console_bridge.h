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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "command_registry.h"
#include "console_context.h"
#include "goldfish/metrics/metrics_reporter.h"
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
     * @param metrics_reporter Reporter for sending telnet command usage metrics.
     */
    LegacyConsoleBridge(int port, std::filesystem::path token_path,
                        goldfish::metrics::MetricsReporter& metrics_reporter);

    ~LegacyConsoleBridge() override;

    // LineCommandHandler implementation
    absl::StatusOr<std::string> operator()(std::string line, Context& ctx) override;
    std::string WelcomeMessage(const Context& ctx) const override;

    std::unique_ptr<Context> CreateContext() const override;

    // Flushes any accumulated telnet command metrics to the metrics reporter.
    void ReportMetrics();

  private:
    void RecordCommand(std::string_view top_level_name, std::string_view canonical_path);

    int port_;
    std::filesystem::path token_path_;
    goldfish::metrics::MetricsReporter& metrics_reporter_;
    std::unique_ptr<CommandRegistry> registry_;

    absl::Mutex metrics_mutex_;
    absl::flat_hash_map<std::string_view, int64_t> command_counts_ ABSL_GUARDED_BY(metrics_mutex_);
};

}  // namespace goldfish::telnet
