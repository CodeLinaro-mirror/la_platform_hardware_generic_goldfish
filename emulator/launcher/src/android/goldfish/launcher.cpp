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
#include <aemu/base/process/Command.h>
#include <android/cmdline-definitions.h>
#include <android/goldfish/devices/device.h>

#include <string>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

#include "android/base/bazel/bazel_info.h"
#include "android/cmdline-option.h"
#include "android/filesystems/ext4_utils.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/cpu/CpuAccelerator.h"
#include "android/main-help.h"
#include "android/utils/path.h"
#include "android/utils/tempfile.h"

// ABSL_FLAG(std::string, avd, "V", "The avd to launch.");
// ABSL_FLAG(bool, list_avds, false, "List available avds");
// ABSL_FLAG(bool, wipe_data, false, "Wipe data and create partitions etc.");
// ABSL_FLAG(bool, verbose, false, "Verbose");
// ABSL_FLAG(std::string, vnc, "",
//           "vnc configuration to use, if any. These will be passed to QEMU as "
//           "-display vnc=<...>");
// ABSL_FLAG(std::string, logcat, "", "Location to write logcat to");
// ABSL_FLAG(std::string, vmodule, "",
//           "per-module log verbosity level."
//           " Argument is a comma-separated list of <module name>=<log level>."
//           " <module name> is a glob pattern, matched against the filename base"
//           " (that is, name ignoring .cc/.h./-inl.h)."
//           " A pattern without slashes matches just the file name portion, otherwise"
//           " the whole file path below the workspace root"
//           " (still without .cc/.h./-inl.h) is matched."
//           " ? and * in the glob pattern match any single or sequence of characters"
//           " respectively including slashes."
//           " <log level> desired log level for the matching modules.");

using android::base::Bazel;
using android::goldfish::Avd;
using android::goldfish::Emulator;

/**
 * @brief Configures the logging behavior based on command-line options.
 *
 * Sets the minimum log level and handles per-module log level settings.
 *
 * @param opts The AndroidOptions struct containing the command-line options.
 */
static void configureLogging(const AndroidOptions& opts) {
    absl::LogSeverityAtLeast logLevel =
            opts.verbose ? absl::LogSeverityAtLeast::kInfo : absl::LogSeverityAtLeast::kWarning;
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::SetMinLogLevel(logLevel);

    if (!opts.vmodule) {
        return;
    }

    std::vector<std::pair<std::string_view, int>> glob_levels;
    for (absl::string_view glob_level : absl::StrSplit(opts.vmodule, '|')) {
        const size_t eq = glob_level.rfind('=');
        if (eq == glob_level.npos) continue;
        const absl::string_view glob = glob_level.substr(0, eq);
        int level;
        if (!absl::SimpleAtoi(glob_level.substr(eq + 1), &level)) continue;
        glob_levels.emplace_back(glob, level);
    }
    for (const auto& it : glob_levels) {
        const absl::string_view glob = it.first;
        const int level = it.second;
        absl::SetVLogLevel(glob, level);
    }
}

int main(int argc, char** argv) {
    absl::InitializeLog();
    absl::log_internal::EnableSymbolizeLogStackTrace(true);

    for (int nn = 1; nn < argc; nn++) {
        const char* opt = argv[nn];
        int helpStatus = emulator_parseHelpOption(opt);
        if (helpStatus >= 0) {
            return helpStatus;
        }
    }

    AndroidOptions opts;
    if (android_parse_options(&argc, &argv, &opts) < 0) {
        return 1;
    }
    configureLogging(opts);

    if (opts.list_avds) {
        auto avds = Avd::list();
        for (const auto& name : avds) {
            auto a = Avd::fromName(name);
            if (!a.status().ok()) {
                std::cout << name << "is not valid: " << a.status().message();
            } else {
                std::cout << a->details(opts.verbose) << '\n';
            }
        }
        return 0;
    }

    Bazel::storeCommandLineArgs(argc, argv);
    std::cout << "Welcome to goldfish \U0001F420, the android emulator launcher\n";

    auto name = opts.avd;
    auto avd = Avd::fromName(name);
    if (!avd.ok()) {
        LOG(ERROR) << "Failed to load " << name << " due to " << avd.status().message();
        return -1;
    }

    LOG(INFO) << "Creating emulator";
    Emulator emulator{std::move(avd.value()), opts};

    if (opts.wipe_data) {
        emulator.clear();
    }

    (void)emulator.launch();
    return 0;
}
