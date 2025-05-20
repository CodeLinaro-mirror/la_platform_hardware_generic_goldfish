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
#include <string>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "aemu_version.h"
#include "android/base/bazel/bazel_info.h"
#include "android/base/system/System.h"
#include "android/cmdline-option.h"
#include "android/crashreport/crash-initializer.h"
#include "android/crashreport/CrashReporter.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/main-help.h"

using android::base::Bazel;
using android::base::System;
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
    absl::LogSeverityAtLeast launcherLogLevel =
            opts.verbose ? absl::LogSeverityAtLeast::kInfo : absl::LogSeverityAtLeast::kWarning;
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::SetMinLogLevel(launcherLogLevel);

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

static void show_banner() {
    constexpr std::string_view platform = PLATFORM " (" TARGET_CPU "), " COMPILATION_MODE;
    std::cout << "              .: .          \n";
    std::cout << "            .    -            Welcome to goldfish\n";
    std::cout << "        ==:    .-+       =-   The android emulator\n";
    std::cout << "     :+            :  #:  .   Version: " VERSION << "-" << BUILD_ID << "\n";
    std::cout << "    %     @         :@-   -   Platform: " << platform << "\n";
    std::cout << "   :              *=   - -    Copyright 2024 The Android Open Source Project\n";
    std::cout << "   :  -<      :+++      - \n";
    std::cout << "    = _ _.*= .            \n";
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
            auto a = Avd::fromName(name, opts.sysdir ? opts.sysdir : "");
            if (!a.status().ok()) {
                std::cout << name << "is not valid: " << a.status().message();
            } else {
                std::cout << (*a)->details(opts.verbose) << '\n';
            }
        }
        return 0;
    }

    Bazel::storeCommandLineArgs(argc, argv);
    show_banner();
    if (!crashhandler_init(argc, argv)) {
        LOG(WARNING) << "Failed to initialize crashreporting.";
    }

    // Setup the library search dirs.
    android::goldfish::fs::path qemu_module_dir;
    if (Bazel::inBazel()) {
        // We are running in the bazel environment, make sure the plugins can be
        // found.
        qemu_module_dir = android::goldfish::fs::path(
                Bazel::runfilesPath("_main/hardware/generic/goldfish/emulator/launcher/plugins"));
        assert(android::goldfish::fs::exists(qemu_module_dir));
    } else {
        qemu_module_dir = System::get()->getProgramDirectory() / "lib" / "qemu";
    }

    // Make sure the child process is using the same crashpad handler as we are using.
    std::stringstream handler;
    handler << android::crashreport::CrashReporter::handlerExe();
    System::get()->setEnvironmentVariable("AEMU_CRASHPAD_HANDLER", handler.str());
    System::get()->setEnvironmentVariable("QEMU_MODULE_DIR", System::pathAsString(qemu_module_dir));
    System::get()->setEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR",
                                          System::pathAsString(qemu_module_dir));
    System::get()->addLibrarySearchDir(qemu_module_dir);

    LOG(INFO) << "Using crashpad handler: " << handler.str();
    LOG(INFO) << "Using module dir: " << qemu_module_dir;

    if (!opts.avd) {
        LOG(ERROR) << "No AVD specified. Use '@foo' or '-avd foo' to launch a virtual device named "
                      "'foo'";
        return -1;
    }

    auto name = opts.avd;
    auto avd = Avd::fromName(name, opts.sysdir ? opts.sysdir : "");
    if (!avd.ok()) {
        LOG(ERROR) << "Failed to load " << name << " due to " << avd.status().message();
        return -1;
    }

    Emulator emulator{std::move(avd.value()), opts};

    if (opts.wipe_data) {
        emulator.clear();
    }

    (void)emulator.launch();
    return 0;
}
