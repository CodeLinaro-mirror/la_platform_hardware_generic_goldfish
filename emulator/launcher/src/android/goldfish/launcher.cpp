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
#include <android/goldfish/devices/device.h>

#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/internal/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "android/base/bazel/bazel_info.h"
#include "android/filesystems/ext4_utils.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/cpu/CpuAccelerator.h"
#include "android/utils/path.h"
#include "android/utils/tempfile.h"

ABSL_FLAG(std::string, avd, "V", "The avd to launch.");
ABSL_FLAG(bool, list_avds, false, "List available avds");
ABSL_FLAG(bool, wipe_data, false, "Wipe data and create partitions etc.");
ABSL_FLAG(bool, verbose, false, "Verbose");
ABSL_FLAG(std::string, vnc, "",
          "vnc configuration to use, if any. These will be passed to QEMU as "
          "-display vnc=<...>");
ABSL_FLAG(std::string, logcat, "/dev/stdout", "Location to write logcat to");
ABSL_FLAG(std::string, vmodule, "",
          "per-module log verbosity level."
          " Argument is a comma-separated list of <module name>=<log level>."
          " <module name> is a glob pattern, matched against the filename base"
          " (that is, name ignoring .cc/.h./-inl.h)."
          " A pattern without slashes matches just the file name portion, otherwise"
          " the whole file path below the workspace root"
          " (still without .cc/.h./-inl.h) is matched."
          " ? and * in the glob pattern match any single or sequence of characters"
          " respectively including slashes."
          " <log level> desired log level for the matching modules.");

using android::base::Bazel;
using android::goldfish::Avd;
using android::goldfish::Emulator;

int main(int argc, char** argv) {
    absl::InitializeLog();
    absl::log_internal::EnableSymbolizeLogStackTrace(true);
    absl::SetProgramUsageMessage("Welcome to goldfish \U0001F420, the android emulator launcher");
    absl::ParseCommandLine(argc, argv);

    absl::LogSeverityAtLeast logLevel = absl::GetFlag(FLAGS_verbose)
                                                ? absl::LogSeverityAtLeast::kInfo
                                                : absl::LogSeverityAtLeast::kWarning;
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::SetMinLogLevel(logLevel);
    Bazel::storeCommandLineArgs(argc, argv);
    std::cout << "Welcome to goldfish \U0001F420, the android emulator launcher\n";

    if (absl::GetFlag(FLAGS_list_avds)) {
        auto avds = Avd::list();
        for (const auto& name : avds) {
            auto a = Avd::fromName(name);
            if (!a.status().ok()) {
                std::cout << name << "is not valid: " << a.status().message();
            } else {
                std::cout << a->details() << '\n';
            }
        }
        return 0;
    }

    auto name = absl::GetFlag(FLAGS_avd);
    auto status = Avd::fromName(name);
    if (!status.ok()) {
        LOG(FATAL) << "Failed to load " << name << " due to " << status.status().message();
    }

    LOG(INFO) << "Creating emulator";
    std::vector<std::string> additionalParams;
    if (!absl::GetFlag(FLAGS_vnc).empty()) {
        additionalParams.push_back("-display");
        additionalParams.push_back(absl::StrCat("vnc=", absl::GetFlag(FLAGS_vnc)));
    }

    if (!absl::GetFlag(FLAGS_logcat).empty()) {
        additionalParams.push_back("-chardev");
        additionalParams.push_back(
                absl::StrCat("file,id=forhvc1,path=", absl::GetFlag(FLAGS_logcat)));
    }

    Emulator emulator{std::move(status.value()), static_cast<int>(logLevel),
                      std::move(absl::GetFlag(FLAGS_vmodule)), std::move(additionalParams)};

    if (absl::GetFlag(FLAGS_wipe_data)) {
        emulator.clear();
    }

    (void)emulator.launch();
    return 0;
}
