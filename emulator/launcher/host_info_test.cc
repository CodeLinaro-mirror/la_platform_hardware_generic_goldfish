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

#include <gtest/gtest.h>

#include "android/base/testing/test_system.h"

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

TEST(HostInfoTest, RunningInCiTrueWithBuildId) {
    TestSystem test_sys("/tmp", "/home", "");
    test_sys.EnvSet("BUILD_ID", "12345");
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

}  // namespace android::goldfish
