// Copyright 2026 The Android Open Source Project
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

#include "plugin_init.h"

#include <string>
#include <string_view>
#include <utility>

#include "absl/base/log_severity.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "android/base/system.h"
#include "goldfish/metrics/metrics_reporter.h"

namespace android::goldfish {
namespace {

constexpr std::string_view kPassthroughLoggingAndCrashVars[] = {
    "AEMU_NO_LOG_SINK",
    "ANDROID_EMU_ENABLE_CRASH_REPORTING",
    "ANDROID_EMU_CRASH_REPORTING_DATABASE",
    "ANDROID_EMU_PERFETTO_TRACE",
    "ANDROID_TMP",
};

}  // namespace

absl::flat_hash_map<std::string, std::string> PluginInit::getQemuEnv(
        const EmulatorConfig& emulator) const {
    const auto& o = emulator.opts();
    auto* sys = android::base::System::Get();

    bool enableInfoLevel = o.verbose || o.V || o.vmodule || o.debug_metrics;
    int pluginLogLevel = static_cast<int>(enableInfoLevel ? absl::LogSeverityAtLeast::kInfo
                                                          : absl::LogSeverityAtLeast::kWarning);

    std::string vmodules;
    if (o.vmodule && *o.vmodule != '\0') {
        vmodules = o.vmodule;
    }
    if (o.debug_metrics) {
        if (!vmodules.empty()) {
            vmodules += ",";
        }
        absl::StrAppend(&vmodules,
                        "metrics_reporter=", ::goldfish::metrics::kMetricsWriterDebugVlogLevel);
    }

    std::string_view vlog_level = o.V ? o.V : "";

    VLOG(1) << "Setting AEMU_LOG_LEVEL to " << pluginLogLevel;
    VLOG(1) << "Setting AEMU_VLOG_LEVEL to " << vlog_level;
    VLOG(1) << "Setting AEMU_LOG_DETAILED to " << (o.verbose ? "true" : "false");

    absl::flat_hash_map<std::string, std::string> env = {
        {"AEMU_LOG_LEVEL", absl::StrCat(pluginLogLevel)},
        {"AEMU_VLOG_LEVEL", std::string(vlog_level)},
        {"AEMU_LOG_DETAILED", o.verbose ? "true" : "false"},
        {"AEMU_CRASHPAD_HANDLER", emulator.emulator_paths().crashpad_handler_binary.string()},
        // ANDROID_EMULATOR_LIBRARY_DIR is used by QEMU's aemu_main.c to locate
        // the goldfish plugin library and by gfxstream to locate the GL and
        // Vulkan libraries.
        {"ANDROID_EMULATOR_LIBRARY_DIR", emulator.emulator_paths().library_directory.string()},
    };
    if (!vmodules.empty()) {
        VLOG(1) << "Setting AEMU_VMODULE to " << vmodules;
        env["AEMU_VMODULE"] = std::move(vmodules);
    }
    if (o.wait_for_debugger) {
        // This is used to wait for the debugger at qemu process start.
        env["ANDROID_EMU_WAIT_FOR_DEBUGGER"] = "1";
    }
    for (std::string_view var : kPassthroughLoggingAndCrashVars) {
        if (std::string val = sys->EnvGet(var); !val.empty()) {
            env.emplace(var, std::move(val));
        }
    }
    return env;
}

}  // namespace android::goldfish
