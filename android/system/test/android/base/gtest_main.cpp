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
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"

#include "android/base/bazel/bazel_info.h"

using android::base::Bazel;

int main(int argc, char* argv[]) {
    if (Bazel::inBazel()) {
        Bazel::storeCommandLineArgs(argc, argv);
    }

    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
