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

#include "env_passthrough_device.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/base/testing/test_system.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST(EnvPassthroughDeviceTest, EmptyWhenNoAllowedVarsSet) {
    base::TestSystem sys("");
    FakeEmulator emu;

    EnvPassthroughDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), IsEmpty());
    EXPECT_THAT(dev.getQemuEnv(emu.config()), IsEmpty());
}

TEST(EnvPassthroughDeviceTest, PassesThroughAllowedVarsAndIgnoresDisallowedVars) {
    base::TestSystem sys("");
    sys.EnvSet("PATH", "/usr/bin:/bin");
    sys.EnvSet("XDG_RUNTIME_DIR", "/run/user/1000");
    sys.EnvSet("HOME", "/home/user");
    sys.EnvSet("UNALLOWED_SECRET_VAR", "should_not_be_passed");

    FakeEmulator emu;

    EnvPassthroughDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("PATH", "/usr/bin:/bin"),
                                     Pair("XDG_RUNTIME_DIR", "/run/user/1000"),
                                     Pair("HOME", "/home/user")));
}

}  // namespace android::goldfish::test
