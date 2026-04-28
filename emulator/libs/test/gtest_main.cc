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

#include <vector>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "android/base/bazel_info.h"
#include "android/base/system.h"

using android::base::Bazel;
using android::base::System;

ABSL_FLAG(bool, verbose_test, false,
          "Enable verbose test logging, equivalent to "
          "--vmodule=\"*=1\" --logtostderr=true --stderrthreshold=0.");

namespace {

void setup_sanitizers() {
#ifdef __linux__
    System::SetEnvironmentVariable("LLVM_SYMBOLIZER", Bazel::RunfilesPath("goldfish_build++toolchain+clang_linux_x64/bin/llvm-symbolizer"));
#elifdef __APPLE__
    System::SetEnvironmentVariable("LLVM_SYMBOLIZER", Bazel::RunfilesPath("goldfish_build++toolchain+clang_mac_all/bin/llvm-symbolizer"));
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    System::SetEnvironmentVariable("LSAN_OPTIONS", absl::StrCat("suppressions=", Bazel::RunfilesPath("goldfish+/emulator/libs/test/leak_suppressions.txt")));
    System::SetEnvironmentVariable("ASAN_OPTIONS", "detect_odr_violation=0");
#endif
#if __has_feature(thread_sanitizer)
    System::SetEnvironmentVariable("TSAN_OPTIONS", absl::StrCat("second_deadlock_stack=1,die_after_fork=0,suppressions=", Bazel::RunfilesPath("goldfish+/emulator/libs/test/tsan_suppressions.txt")));
#endif
#endif
}

} // namespace

int main(int argc, char* argv[]) {
    absl::InitializeSymbolizer(argv[0]);

    absl::InstallFailureSignalHandler({});

    if (Bazel::InBazel()) {
        Bazel::StoreCommandLineArgs(argc, argv);
    }

    setup_sanitizers();

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
