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

#include "goldfish/tools/aemu_version.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace goldfish::version {
namespace {

TEST(AemuVersionTest, GetEmulatorVersionMatchesMacro) {
    EXPECT_EQ(GetEmulatorVersion(), VERSION);
    EXPECT_FALSE(GetEmulatorVersion().empty());
}

TEST(AemuVersionTest, GetEmulatorPlatformMatchesMacro) {
    EXPECT_EQ(GetEmulatorPlatform(), PLATFORM);
    EXPECT_FALSE(GetEmulatorPlatform().empty());
}

TEST(AemuVersionTest, GetEmulatorTargetCpuMatchesMacro) {
    EXPECT_EQ(GetEmulatorTargetCpu(), TARGET_CPU);
    EXPECT_FALSE(GetEmulatorTargetCpu().empty());
}

TEST(AemuVersionTest, GetEmulatorCompilationModeMatchesMacro) {
    EXPECT_EQ(GetEmulatorCompilationMode(), COMPILATION_MODE);
    EXPECT_FALSE(GetEmulatorCompilationMode().empty());
}

TEST(AemuVersionTest, GetPlatformStringMatchesExpected) {
    EXPECT_FALSE(GetPlatformString().empty());
    std::string expected = std::string(GetEmulatorPlatform()) + " (" +
                           std::string(GetEmulatorTargetCpu()) + "), " +
                           std::string(GetEmulatorCompilationMode());
    EXPECT_EQ(GetPlatformString(), expected);
    EXPECT_EQ(GetEmulatorPlatformString(), expected);
}

TEST(AemuVersionTest, GetEmulatorBuildIdNotEmpty) {
    EXPECT_FALSE(GetEmulatorBuildId().empty());
}

TEST(AemuVersionTest, GetEmulatorFullVersionFormat) {
    std::string_view full_version = GetEmulatorFullVersion();
    std::string expected =
            std::string(GetEmulatorVersion()) + "-" + std::string(GetEmulatorBuildId());
    EXPECT_EQ(full_version, expected);
}

}  // namespace
}  // namespace goldfish::version
