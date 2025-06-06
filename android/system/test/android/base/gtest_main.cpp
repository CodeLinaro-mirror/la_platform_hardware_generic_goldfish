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
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "gtest/gtest.h"

#include "android/base/bazel/bazel_info.h"

using android::base::Bazel;

int main(int argc, char* argv[]) {
    if (Bazel::inBazel()) {
        Bazel::storeCommandLineArgs(argc, argv);
    }

    // Parse abseil logging configuration.
    std::vector<char*> positional_args;
    std::vector<absl::UnrecognizedFlag> unrecognized_flags;
    absl::ParseAbseilFlagsOnly(argc, argv, positional_args, unrecognized_flags);

    absl::InitializeLog();

    // Note: Google Test will ignore all the abseil flags
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
