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

#include "android/goldfish/feature_flags.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "absl/status/status_matchers.h"

#include "android/base/testing/TestSystem.h"
#include "android/status/status_matcher_macros.h"

namespace android::goldfish {

TEST(FeatureFlags, parseFeatureFile) {
    base::TestSystem sys("bin", "myhome");
    fs::path temp_file = sys.GetTempRoot()->Path() / "advancedFeatures.ini";

    std::ofstream out(temp_file);
    out << "# Test advanced features file\n"
        << "mac80211hwsimUserspaceManaged=off\n"
        << "mac80211hwsimUserspaceManaged=on\n"
        << "GLPipeChecksum=oN\n"
        << "GrallocSync = ON\n"
        << "GLAsyncSwap = OFF\n"
        << "EncryptUserData = foo\n"
        << "RefCountPipe\n"
        << "someUnknownFeature=off\n"
        << "\t\tsomewhitespace=On\n"
        << "feature = Off\n"
        << "I did something = wrong =h ere\n";
    out.close();

    ASSERT_OK_AND_ASSIGN(auto res, ParseFeatureFile(temp_file));
    EXPECT_THAT(
            res,
            ::testing::UnorderedElementsAreArray({
                std::pair{android_studio::EmulatorFeatureFlagState::MAC80211HWSIM_USERSPACE_MANAGED,
                          FeatureStatus::On},
                {android_studio::EmulatorFeatureFlagState::GL_PIPE_CHECKSUM, FeatureStatus::On},
                {android_studio::EmulatorFeatureFlagState::GRALLOC_SYNC, FeatureStatus::On},
                {android_studio::EmulatorFeatureFlagState::GL_ASYNC_SWAP, FeatureStatus::Off},
                {android_studio::EmulatorFeatureFlagState::ENCRYPT_USER_DATA,
                 FeatureStatus::Missing},
            }));
}

TEST(FeatureFlags, parseFeatureFileNotFound) {
    auto result_or = ParseFeatureFile("nonexistent_file.ini");
    EXPECT_FALSE(result_or.ok());
}

}  // namespace android::goldfish