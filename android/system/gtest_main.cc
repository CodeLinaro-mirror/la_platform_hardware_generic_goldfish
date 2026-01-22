// Copyright 2015 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "gtest/gtest.h"

#include "android/base/bazel_info.h"

using android::base::Bazel;

ABSL_FLAG(bool, verbose_test, false,
          "Enable verbose test logging, equivalent to "
          "--vmodule=\"*=1\" --logtostderr=true --stderrthreshold=0.");

int main(int argc, char* argv[]) {
    absl::InitializeSymbolizer(argv[0]);

    absl::InstallFailureSignalHandler({});

    if (Bazel::InBazel()) {
        Bazel::StoreCommandLineArgs(argc, argv);
    }

    // Parse abseil logging configuration.
    std::vector<char*> positional_args;
    std::vector<absl::UnrecognizedFlag> unrecognized_flags;
    absl::ParseAbseilFlagsOnly(argc, argv, positional_args, unrecognized_flags);

    if (absl::GetFlag(FLAGS_verbose_test)) {
        absl::SetVLogLevel("*", 1);
        absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    }

    absl::InitializeLog();

    // Note: Google Test will ignore all the abseil flags
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
