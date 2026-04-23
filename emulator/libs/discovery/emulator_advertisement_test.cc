// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/discovery/emulator_advertisement.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"

#include "android/base/testing/TestSystem.h"

using android::base::TestSystem;
namespace fs = std::filesystem;

namespace android {
namespace goldfish {

class EmulatorAdvertisementTest : public testing::TestWithParam<bool> {};

TEST_P(EmulatorAdvertisementTest, getDiscoveryDirectory) {
    TestSystem sys("", "myhome");

    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("runtime")));
    fs::path base = sys.GetTempRoot()->Path() / "runtime";

#if defined(_WIN32)
    base = base / "Temp";
#elif defined(__APPLE__)
    base = base / "Library" / "Caches" / "TemporaryItems";
#endif

    auto want = base / "avd" / "running";
    if (GetParam()) {
        ASSERT_TRUE(
                android::base::file::mkdir_recursive(sys.GetTempRoot()->Path() / want, 0755).ok())
                << "creating: " << want;
        // Make sure that unreadable dir can be fixed.
        android::base::file::chmod(want, 0055).IgnoreError();
    }

    sys.EnvSet("LOCALAPPDATA", (sys.GetTempRoot()->Path() / "runtime").string());
    sys.EnvSet("XDG_RUNTIME_DIR", (sys.GetTempRoot()->Path() / "runtime").string());
    sys.EnvSet("HOME", (sys.GetTempRoot()->Path() / "runtime").string());

    auto got = EmulatorAdvertisement::GetDiscoveryDirectory();
    EXPECT_THAT(got.string(), testing::EndsWith(want.string()));
    EXPECT_TRUE(android::base::file::exists(got));
    // On Windows this will return 0777 (instead of 0755) as there's only one read-only bit.
    EXPECT_THAT(android::base::file::mode(got),
                absl_testing::IsOkAndHolds(
                        ::testing::Truly([](unsigned mode) { return (mode & 0700) == 0700; })));
}

INSTANTIATE_TEST_SUITE_P(DiscoveryDirectory, EmulatorAdvertisementTest,
                         testing::Values(true, false));

}  // namespace goldfish
}  // namespace android
