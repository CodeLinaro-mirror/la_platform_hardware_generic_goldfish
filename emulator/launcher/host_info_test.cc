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

#include "host_info.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "android/base/testing/test_system.h"
#include "android/goldfish/mock_avd.h"
#include "studio_stats.pb.h"

using android::base::TestSystem;
using android::goldfish::MockAvd;
using ::testing::Return;
using ::testing::ReturnRef;

namespace android::goldfish {

using android::base::TestSystem;

TEST(HostInfoTest, RunningInCiDefaultFalse) {
    TestSystem test_sys("/tmp", "/home", "");
    EXPECT_FALSE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiTrueWithCi1) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("CI", "1");
    EXPECT_TRUE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiTrueWithCiTrue) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("CI", "true");
    EXPECT_TRUE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiTrueWithContinuousIntegration) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("CONTINUOUS_INTEGRATION", "1");
    EXPECT_TRUE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiTrueWithGithubActions) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("GITHUB_ACTIONS", "true");
    EXPECT_TRUE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiTrueWithGitlabCi) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("GITLAB_CI", "1");
    EXPECT_TRUE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiTrueWithJenkins) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("JENKINS_URL", "http://jenkins.example.com");
    EXPECT_TRUE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiFalseWithBuildIdAlone) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("BUILD_ID", "12345");
    EXPECT_FALSE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiTrueWithTfBuild) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("TF_BUILD", "1");
    EXPECT_TRUE(IsRunningInCi());
}

TEST(HostInfoTest, RunningInCiTrueWithCircleCi) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("CIRCLECI", "true");
    EXPECT_TRUE(IsRunningInCi());
}

TEST(HostInfoTest, AndroidCliDefinedDefaultFalse) {
    TestSystem test_sys("/tmp", "/home", "");
    EXPECT_FALSE(IsAndroidCliDefined());
}

TEST(HostInfoTest, AndroidCliDefinedFalseWhenNotOne) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("ANDROID_CLI", "0");
    EXPECT_FALSE(IsAndroidCliDefined());
    test_sys.EnvSet("ANDROID_CLI", "true");
    EXPECT_FALSE(IsAndroidCliDefined());
}

TEST(HostInfoTest, AndroidCliDefinedTrueWhenOne) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("ANDROID_CLI", "1");
    EXPECT_TRUE(IsAndroidCliDefined());
}

TEST(HostInfoTest, RunningInContainerWithEnvVar) {
    TestSystem test_sys("/tmp", "/home", "");
#if defined(__linux__)
    test_sys.EnvSet("container", "podman");
    EXPECT_TRUE(IsRunningInContainer());
#endif
}

TEST(HostInfoTest, RunningInContainerWithK8s) {
    TestSystem test_sys("/tmp", "/home", "");
#if defined(__linux__)
    test_sys.EnvSet("KUBERNETES_SERVICE_HOST", "10.0.0.1");
    EXPECT_TRUE(IsRunningInContainer());
#endif
}

class HostInfoVulkanTest : public ::testing::Test {
  protected:
    void SetUp() override {
        EXPECT_CALL(avd_, ApiLevel()).WillRepeatedly(Return(37));
        EXPECT_CALL(avd_, Arch()).WillRepeatedly(Return(Avd::CpuArchitecture::kArm));
        EXPECT_CALL(avd_, BuildId())
                .WillRepeatedly(Return("google/generic_system_google/generic:17/CE2A.260420.008/"
                                       "15505217:userdebug/dev-keys"));
        EXPECT_CALL(avd_, BuildTimestamp()).WillRepeatedly(Return(1779905333));
        EXPECT_CALL(avd_, DisplayName()).WillRepeatedly(Return("PIXEL_8"));
        EXPECT_CALL(avd_, GetSystemImagePaths()).WillRepeatedly(ReturnRef(sys_image_paths_));
        EXPECT_CALL(avd_, Hw()).WillRepeatedly(ReturnRef(hw_config_));
    }

    MockAvd avd_;
    SystemImagePaths sys_image_paths_;
    HardwareConfig hw_config_;
};

TEST_F(HostInfoVulkanTest, setVulkanIcdHostDefault) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_EMU_VK_ICD", "");

    android_studio::AndroidStudioEvent event;
    FillEmulatorHostEvent(event, avd_, 1234, 5678, true, false);

    EXPECT_EQ(event.emulator_details().vulkan_icd(),
              android_studio::EmulatorDetails::HOST_DEFAULT_VK);
}

TEST_F(HostInfoVulkanTest, setVulkanIcdSwiftshader) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_EMU_VK_ICD", "swiftshader");

    android_studio::AndroidStudioEvent event;
    FillEmulatorHostEvent(event, avd_, 1234, 5678, true, false);

    EXPECT_EQ(event.emulator_details().vulkan_icd(),
              android_studio::EmulatorDetails::SWIFTSHADER_VK);
}

TEST_F(HostInfoVulkanTest, setVulkanIcdLavapipe) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_EMU_VK_ICD", "lavapipe");

    android_studio::AndroidStudioEvent event;
    FillEmulatorHostEvent(event, avd_, 1234, 5678, true, false);

    EXPECT_EQ(event.emulator_details().vulkan_icd(), android_studio::EmulatorDetails::LAVAPIPE_VK);
}

TEST_F(HostInfoVulkanTest, setVulkanIcdMoltenvk) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_EMU_VK_ICD", "moltenvk");

    android_studio::AndroidStudioEvent event;
    FillEmulatorHostEvent(event, avd_, 1234, 5678, true, false);

    EXPECT_EQ(event.emulator_details().vulkan_icd(), android_studio::EmulatorDetails::MOLTEN_VK);
}

TEST_F(HostInfoVulkanTest, setVulkanIcdKosmickrisp) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_EMU_VK_ICD", "kosmickrisp");

    android_studio::AndroidStudioEvent event;
    FillEmulatorHostEvent(event, avd_, 1234, 5678, true, false);

    EXPECT_EQ(event.emulator_details().vulkan_icd(),
              android_studio::EmulatorDetails::KOSMICKRISP_VK);
}

TEST_F(HostInfoVulkanTest, setVulkanIcdUnknown) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_EMU_VK_ICD", "unknown_icd");

    android_studio::AndroidStudioEvent event;
    FillEmulatorHostEvent(event, avd_, 1234, 5678, true, false);

    EXPECT_EQ(event.emulator_details().vulkan_icd(), android_studio::EmulatorDetails::UNKNOWN_VK);
}

}  // namespace android::goldfish
