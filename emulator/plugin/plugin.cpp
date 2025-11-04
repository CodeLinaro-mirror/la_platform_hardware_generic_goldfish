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

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "android/base/system/System.h"
#include "android/crashreport/crash-initializer.h"
#include "android/crashreport/CrashReporter.h"

#include "goldfish/adb/adb-device.h"
#include "goldfish/avd/avd-finalize.h"
#include "goldfish/avd/avd-info.h"
#include "goldfish/avd/global-event-loop.h"
#include "goldfish/battery/goldfish_battery.h"
#include "goldfish/grpc/grpc.h"
#include "goldfish/input/virtio-input-android.h"
#include "goldfish/net/virtio-wifi.h"
#include "goldfish/netsim/netsim-chardev.h"
#include "goldfish/netsim/netsim-netdev.h"
#include "goldfish/vsock/vsock_low_level.h"
#include "goldfish/vsock/vsock_port_fwd.h"
#include "goldfish/tools/aemu_version.h"

// library and initialize the crashpad crash engine upon launch.
#include "google/system/aemu_func_defs.h"

extern "C" {
    #include "qemu/error-report.h"
}

namespace {

using android::base::System;

void setup_debug_logging() {
    std::string v_str = System::get()->getEnvironmentVariable("AEMU_VLOG_LEVEL");
    if (!v_str.empty()) {
        if (int v_level; !absl::SimpleAtoi(v_str, &v_level)) {
            LOG(ERROR) << "AEMU_VLOG_LEVEL was set to an invalid value: " << v_str;
        } else {
          absl::SetGlobalVLogLevel(v_level);
        }
    }

    if (std::string vmodule = System::get()->getEnvironmentVariable("AEMU_VMODULE");
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
    std::string log_level_str = System::get()->getEnvironmentVariable("AEMU_LOG_LEVEL");
    if (log_level_str.empty()) {
        return default_log_level;
    }
    int log_level;
    if (!absl::SimpleAtoi(log_level_str, &log_level)) {
        LOG(ERROR) << "AEMU_LOG_LEVEL was set to an invalid value: " << log_level_str;
        return default_log_level;
    }

    if (log_level < 0 || log_level > 4) {
        LOG(ERROR) << "AEMU_LOG_LEVEL should be in the range [0, 3] (info, warning, error, fatal), "
                      "not: "
                   << log_level;
        return default_log_level;
    }
    return log_level;
}

void setup_logging() {
    absl::InitializeLog();
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
    int log_level = get_log_level();
    absl::SetStderrThreshold(static_cast<absl::LogSeverityAtLeast>(log_level));
    setup_debug_logging();

    // Disable Qemu info level logging if necessary.
    error_set_log_info(log_level == 0);
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
    goldfish::netsim::netsim_netdev_register_types();
    goldfish::netsim::netsim_chardev_register_types();
    VLOG(1) << "Exit GF_REGISTER_TYPES";
}

extern "C" void GF_STARTUP_FUNC(int argc, char** argv) {
    absl::InitializeSymbolizer(argv[0]);

    setup_logging();

    VLOG(1) << "Goldfish plugin version: " VERSION << "-" << BUILD_ID;
    if (!crashhandler_init(argc, argv)) {
        LOG(WARNING) << "Failed to initialize crashreporting.";
    }

    absl::FailureSignalHandlerOptions options;
    // Call crashpad after printing stack trace.
    options.call_previous_handler = true;
    absl::InstallFailureSignalHandler(options);

    auto* clientLoop = goldfish::async::globalEventLoop();
    android::crashreport::CrashReporter::get()->hangDetector().addWatchedLooper("GlobalEventLoop", *clientLoop, absl::Seconds(15));

    LOG(INFO) << "goldfish plugin initialization completed";
}

extern "C" void GF_SHUTDOWN_FUNC(void) {}
