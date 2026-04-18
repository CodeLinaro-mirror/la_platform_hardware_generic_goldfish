// Copyright (C) 2024 The Android Open Source Project
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

#include <memory>

#include "android/cmdline_option.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/input_paths.h"
#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/process_launcher.h"
#include "goldfish/async/signal_handlers.h"
#include "goldfish/metrics/configure_metrics_writer.h"
#include "goldfish/metrics/metrics_reporter.h"

namespace android::goldfish {

constexpr int kMetricsCrashesNone = 0;
constexpr int kMetricsCrashesAbandoned = 1;
constexpr int kMetricsCrashesUncleanExit = 2;

struct LauncherConfig {
    // Owns every member except event_loop.
    ::goldfish::async::LibuvEventLoop& event_loop;

    std::unique_ptr<::goldfish::async::ProcessLauncher> process_launcher;
    std::unique_ptr<::goldfish::async::SignalHandlers> signal_handlers;
    std::unique_ptr<::goldfish::metrics::MetricsReporter> metrics_reporter;
    std::unique_ptr<::goldfish::async::AsyncSocketFactory> socket_factory;

    UserPaths user_paths;
    EmulatorPaths emulator_paths;
    std::unique_ptr<Avd> avd;
    AndroidOptions opts;
    ::goldfish::metrics::MetricsWriterConfig metrics_writer_config;
};

// Runs the emulator launcher and enters the event loop.
// Returns the exit status of the emulator.
int RunLauncher(LauncherConfig config);

inline bool ShouldLaunchFishtank(const AndroidOptions& opts) {
    return !opts.no_window;
}

}  // namespace android::goldfish
