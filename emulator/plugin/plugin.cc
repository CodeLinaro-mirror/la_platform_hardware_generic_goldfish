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

#include <cstring>

#include "absl/base/log_severity.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/log/structured.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "android/base/color_log_sink.h"
#include "android/base/system.h"
#include "android/crashreport/breadcrumb.h"
#include "android/crashreport/crash_reporter.h"
#include "android/crashreport/crash_system.h"
#include "android/crashreport/debug.h"
#include "goldfish/adb_device/adb_device.h"
#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/avd_finalize/avd_finalize.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/avd_info/avd_info_register_types.h"
#include "goldfish/battery/goldfish_battery.h"
#include "goldfish/grpc/grpc.h"
#include "goldfish/input/virtio_input_android.h"
#include "goldfish/net/virtio_wifi.h"
#include "goldfish/netsim/netsim_chardev.h"
#include "goldfish/netsim/netsim_connection.h"
#include "goldfish/netsim/netsim_netdev.h"
#include "goldfish/tools/aemu_version.h"
#include "goldfish/vsock/vsock_low_level.h"
#include "goldfish/vsock/vsock_port_fwd.h"

// library and initialize the crashpad crash engine upon launch.
#include "google/system/aemu_func_defs.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
    #include "qemu/error-report.h"
}
// IWYU pragma: end_keep
// clang-format on

namespace {

using android::base::System;

void qemu_absl_logger(int severity, const char* file, int line, const char* fmt, va_list ap) {
    std::string message(4096, '\0');
    int size = vsnprintf(message.data(), message.size(), fmt, ap);

    if (size <= 0) {
        // Ignore errors and empty strings.
        return;
    }

    if (size >= message.size()) {
        // Indicate truncation.
        strncpy(message.data() + message.size() - 3, "...", 3);
        VLOG(1) << "Following log message truncated, size needed: " << size
                << " truncated to: " << (message.size() - 3);
        size = message.size();
    } else if (message.back() == '\n') {
        // Strip extra \n that qemu adds, our logging framework will add the newline.
        size--;
    }
    std::string_view message_view(message.data(), size);

    // Log error messages as breadcrumbs so they show up in crash reports
    if (severity >= static_cast<int>(absl::LogSeverity::kError)) {
        CRUMB(kQemu) << absl::StrFormat("%s:%d %s\n", file ? file : "QEMU", line, message_view);
    }

    LOG(LEVEL(severity)).AtLocation(file ? file : "QEMU", line) << absl::LogAsLiteral(message_view);
}

void setup_debug_logging() {
    std::string v_str = System::Get()->GetEnvironmentVariable("AEMU_VLOG_LEVEL");
    if (!v_str.empty()) {
        if (int v_level; !absl::SimpleAtoi(v_str, &v_level)) {
            LOG(ERROR) << "AEMU_VLOG_LEVEL was set to an invalid value: " << v_str;
        } else {
            absl::SetGlobalVLogLevel(v_level);
        }
    }

    if (std::string vmodule = System::Get()->GetEnvironmentVariable("AEMU_VMODULE");
        !vmodule.empty()) {
        // TODO share this with launcher.cpp / logging.cpp
        for (const absl::string_view glob_level : absl::StrSplit(vmodule, ',')) {
            const size_t eq = glob_level.rfind('=');
            if (eq == glob_level.npos) continue;
            const absl::string_view glob = glob_level.substr(0, eq);
            int level;
            if (!absl::SimpleAtoi(glob_level.substr(eq + 1), &level)) continue;

            absl::SetVLogLevel(glob, level);
            LOG(INFO) << "Setting module verbosity for " << glob << " to " << level;
        }
    }
}

int get_log_level() {
    // Default to logging only error and fatal.
    static const int default_log_level = 2;
    std::string log_level_str = System::Get()->GetEnvironmentVariable("AEMU_LOG_LEVEL");
    if (log_level_str.empty()) {
        return default_log_level;
    }
    int log_level;
    if (!absl::SimpleAtoi(log_level_str, &log_level)) {
        LOG(ERROR) << "AEMU_LOG_LEVEL was set to an invalid value: " << log_level_str;
        return default_log_level;
    }

    if (log_level < 0 || log_level > 4) {
        LOG(ERROR) << "AEMU_LOG_LEVEL should be in the range [0, 3] (info, "
                      "warning, error, fatal), "
                      "not: "
                   << log_level;
        return default_log_level;
    }
    return log_level;
}

void setup_logging() {
    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    int log_level = get_log_level();
    absl::SetMinLogLevel(static_cast<absl::LogSeverityAtLeast>(log_level));
    setup_debug_logging();

    if (System::Get()->GetEnvironmentVariable("AEMU_NO_LOG_SINK").empty()) {
        static android::base::ColorLogSink logSink(&std::cout, isatty(fileno(stdout)));
        logSink.SetVerbosity(System::Get()->GetEnvironmentVariable("AEMU_LOG_DETAILED") == "true");
        absl::AddLogSink(&logSink);
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);
    }

    // Switch QEMU logging to ABSL.
    set_logger(&qemu_absl_logger);
}

}  // namespace

extern "C" void GF_REGISTER_TYPES_FUNC(void) {
    VLOG(1) << "Enter GF_REGISTER_TYPES";
    goldfish_battery_register_types();
    vsock_port_fwd_register_types();
    vsock_low_level_register_types();
    goldfish::avd_info::avd_info_register_types();
    goldfish::avd_finalize::avd_finalize_register_types();
    goldfish::adb_device::adb_device_register_types();
    virtio_input_android_register_types();
    goldfish::grpc::grpc_register_types();
    virtio_wifi_register_types();
    goldfish::netsim::netsim_connection_register_types();
    goldfish::netsim::netsim_netdev_register_types();
    goldfish::netsim::netsim_chardev_register_types();
    VLOG(1) << "Exit GF_REGISTER_TYPES";
}

extern "C" void GF_STARTUP_FUNC(int argc, char** argv) {
    absl::InitializeSymbolizer(argv[0]);

    setup_logging();

    VLOG(1) << "Goldfish plugin version: " VERSION << "-" << BUILD_ID;
    // The plugin crash system should never try to upload - that should only be
    // done by the launcher.
    if (!android::crashreport::CrashSystem::get().initialize()) {
        LOG(WARNING) << "Failed to initialize crashreporting.";
    }

    absl::FailureSignalHandlerOptions options;
    // Call crashpad after printing stack trace.
    options.call_previous_handler = true;
    absl::InstallFailureSignalHandler(options);

    auto* client_loop = goldfish::async::globalEventLoop();
    android::crashreport::CrashReporter::GetCrashingHangDetector().AddWatchedLooper(
            "GlobalEventLoop", *client_loop, absl::Seconds(15));

    LOG(INFO) << "goldfish plugin initialization completed";

    // Allow waiting for a debugger through a command line argument
    if (System::Get()->GetEnvironmentVariable("ANDROID_EMU_WAIT_FOR_DEBUGGER") == "1") {
        LOG(WARNING) << "Waiting for a debugger...";
        android::base::WaitForDebugger();
        LOG(WARNING) << "Debugger has attached, resuming";
    }
}

extern "C" void GF_SHUTDOWN_FUNC(void) {
    auto* client_loop = goldfish::async::globalEventLoop();
    LOG_IF(FATAL, !client_loop->ShutdownAndWait(std::chrono::seconds(10)).ok())
            << "global event loop shutdown failed within 10s";
    LOG(INFO) << "goldfish plugin shutdown completed";
}
