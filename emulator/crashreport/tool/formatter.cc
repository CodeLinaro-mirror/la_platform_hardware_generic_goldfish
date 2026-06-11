// Copyright 2025 The Android Open Source Project
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
#include "emulator/crashreport/tool/formatter.h"

#include <iomanip>
#include <iostream>

#include "absl/log/log.h"
#include "absl/time/time.h"

#include "processor/stackwalk_common.h"

namespace android {
namespace crashreport {

void Formatter::PrintReportList(const std::vector<crashpad::CrashReportDatabase::Report>& reports) {
    for (const auto& report : reports) {
        absl::Time now_absl = absl::FromTimeT(report.creation_time);
        std::string formatted_time =
                absl::FormatTime("%Y-%m-%d %H:%M:%S %Z", now_absl, absl::LocalTimeZone());
        std::cout << formatted_time << " | " << report.file_path;
        if (report.uploaded) {
            std::cout << " (Uploaded: " << report.id << ")";
        } else {
            std::cout << " (Local)";
        }
        std::cout << std::endl;
    }
}

void Formatter::PrintMinidumpAnalysis(const google_breakpad::ProcessState& process_state,
                                      google_breakpad::BasicSourceLineResolver* resolver,
                                      const nlohmann::json& modules, bool machine_readable,
                                      bool output_stack_contents) {
    if (machine_readable) {
        PrintProcessStateMachineReadable(process_state);
    } else {
        PrintProcessState(process_state, output_stack_contents,
                          /*output_requesting_thread_only=*/false, resolver);
    }

    if (!modules.is_null()) {
        std::cout << "Module annotations:";
        std::cout << "===================";
        std::cout << std::setw(2) << modules;
    }
}

}  // namespace crashreport
}  // namespace android
