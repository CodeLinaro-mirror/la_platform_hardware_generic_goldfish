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

#include "goldfish/update_check/repository_manifest.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"

#include "android/status/status_matcher_macros.h"

namespace goldfish::update_check {
namespace {

using ::absl_testing::StatusIs;

constexpr std::string_view kSampleRepositoryXml = R"xml(<?xml version="1.0" encoding="utf-8"?>
<sdk:sdk-repository
    xmlns:common="http://schemas.android.com/repository/android/common/02"
    xmlns:generic="http://schemas.android.com/repository/android/generic/02"
    xmlns:sdk="http://schemas.android.com/sdk/android/repo/repository2/03"
    xmlns:sdk-common="http://schemas.android.com/sdk/android/repo/common/03"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <channel id="channel-0">stable</channel>
  <channel id="channel-1">beta</channel>
  <channel id="channel-2">dev</channel>
  <channel id="channel-3">canary</channel>

  <!-- Legacy emulator packages have higher 37.2 / 37.3 revisions and macosx/x64 archives,
       and must be ignored when querying "emulators;". -->
  <remotePackage path="emulator">
    <revision><major>37</major><minor>2</minor><micro>12</micro></revision>
    <channelRef ref="channel-0"/>
    <archives>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>aarch64</host-arch></archive>
      <archive><host-os>windows</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>
  <remotePackage path="emulator">
    <revision><major>37</major><minor>3</minor><micro>3</micro></revision>
    <channelRef ref="channel-2"/>
    <archives>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>aarch64</host-arch></archive>
      <archive><host-os>windows</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>

  <!-- Preview emulator packages -->
  <remotePackage path="emulators;16013376">
    <revision><major>37</major><minor>1</minor><micro>1</micro></revision>
    <display-name>Android Emulator (Preview)</display-name>
    <channelRef ref="channel-2"/>
    <archives>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>aarch64</host-arch></archive>
      <archive><host-os>windows</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>

  <remotePackage path="emulators;16173978">
    <revision><major>37</major><minor>1</minor><micro>2</micro></revision>
    <display-name>Android Emulator (Preview)</display-name>
    <channelRef ref="channel-2"/>
    <archives>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>aarch64</host-arch></archive>
      <archive><host-os>windows</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>

  <remotePackage path="emulators;16373346">
    <revision><major>37</major><minor>1</minor><micro>3</micro></revision>
    <display-name>Android Emulator (Preview)</display-name>
    <channelRef ref="channel-2"/>
    <archives>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>aarch64</host-arch></archive>
      <archive><host-os>windows</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>

  <!-- Alias entry must be ignored in favor of numbered emulators;<build_id> entries -->
  <remotePackage path="emulators;latest">
    <revision><major>99</major><minor>9</minor><micro>9</micro></revision>
    <display-name>Android Emulator (Preview) (latest)</display-name>
    <channelRef ref="channel-2"/>
    <archives>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>
</sdk:sdk-repository>
)xml";

TEST(RepositoryManifestTest, FindsLatestPackageForSupportedHosts) {
    ASSERT_OK_AND_ASSIGN(auto latest_linux,
                         FindLatestPackage(kSampleRepositoryXml, "linux", "x64", "emulators;",
                                           UpdateChannel::kCanary));
    ASSERT_TRUE(latest_linux.has_value());
    EXPECT_EQ(*latest_linux, (SdkPackage{{37, 1, 3, 16373346}, UpdateChannel::kDev}));
    EXPECT_EQ(latest_linux->VersionString(), "37.1.3");
    EXPECT_EQ(latest_linux->ToString(), "37.1.3 (build 16373346)");

    ASSERT_OK_AND_ASSIGN(auto latest_mac_arm,
                         FindLatestPackage(kSampleRepositoryXml, "macosx", "aarch64", "emulators;",
                                           UpdateChannel::kCanary));
    ASSERT_TRUE(latest_mac_arm.has_value());
    EXPECT_EQ(*latest_mac_arm, (SdkPackage{{37, 1, 3, 16373346}, UpdateChannel::kDev}));

    ASSERT_OK_AND_ASSIGN(auto latest_win,
                         FindLatestPackage(kSampleRepositoryXml, "windows", "x64", "emulators;",
                                           UpdateChannel::kCanary));
    ASSERT_TRUE(latest_win.has_value());
    EXPECT_EQ(*latest_win, (SdkPackage{{37, 1, 3, 16373346}, UpdateChannel::kDev}));
}

TEST(RepositoryManifestTest, FindsPackageWithoutBuildIdSuffix) {
    ASSERT_OK_AND_ASSIGN(auto latest_emu,
                         FindLatestPackage(kSampleRepositoryXml, "linux", "x64", "emulator",
                                           UpdateChannel::kCanary));
    ASSERT_TRUE(latest_emu.has_value());
    EXPECT_EQ(*latest_emu, (SdkPackage{{37, 3, 3, 0}, UpdateChannel::kDev}));
    EXPECT_EQ(latest_emu->ToString(), "37.3.3");
}

TEST(RepositoryManifestTest, FiltersToChannelsAtLeastAsStableAsMaxChannel) {
    // kSampleRepositoryXml has "emulator" 37.2.12 in channel-0 (stable) and
    // 37.3.3 in channel-2 (dev).
    ASSERT_OK_AND_ASSIGN(auto stable_emu,
                         FindLatestPackage(kSampleRepositoryXml, "linux", "x64", "emulator",
                                           UpdateChannel::kStable));
    ASSERT_TRUE(stable_emu.has_value());
    EXPECT_EQ(*stable_emu, (SdkPackage{{37, 2, 12, 0}, UpdateChannel::kStable}));

    ASSERT_OK_AND_ASSIGN(auto beta_emu,
                         FindLatestPackage(kSampleRepositoryXml, "linux", "x64", "emulator",
                                           UpdateChannel::kBeta));
    ASSERT_TRUE(beta_emu.has_value());
    EXPECT_EQ(*beta_emu, (SdkPackage{{37, 2, 12, 0}, UpdateChannel::kStable}));

    ASSERT_OK_AND_ASSIGN(auto dev_emu,
                         FindLatestPackage(kSampleRepositoryXml, "linux", "x64", "emulator",
                                           UpdateChannel::kDev));
    ASSERT_TRUE(dev_emu.has_value());
    EXPECT_EQ(*dev_emu, (SdkPackage{{37, 3, 3, 0}, UpdateChannel::kDev}));

    // "emulators;*" entries are in channel-2 (dev), so querying with kStable returns nullopt.
    ASSERT_OK_AND_ASSIGN(auto stable_preview,
                         FindLatestPackage(kSampleRepositoryXml, "linux", "x64", "emulators;",
                                           UpdateChannel::kStable));
    EXPECT_EQ(stable_preview, std::nullopt);

    // kUnknown falls back to kStable.
    ASSERT_OK_AND_ASSIGN(auto unknown_emu, FindLatestPackage(kSampleRepositoryXml, "linux", "x64",
                                                             "emulator", UpdateChannel::kUnknown));
    ASSERT_TRUE(unknown_emu.has_value());
    EXPECT_EQ(*unknown_emu, (SdkPackage{{37, 2, 12, 0}, UpdateChannel::kStable}));
}

TEST(RepositoryManifestTest, ReturnsNulloptForUnsupportedHostArch) {
    ASSERT_OK_AND_ASSIGN(auto latest_mac_x64,
                         FindLatestPackage(kSampleRepositoryXml, "macosx", "x64", "emulators;",
                                           UpdateChannel::kCanary));
    EXPECT_EQ(latest_mac_x64, std::nullopt);
}

TEST(RepositoryManifestTest, UsesBuildIdAsTiebreakerForSameRevision) {
    constexpr std::string_view kTiebreakerXml = R"xml(
<sdk-repository xmlns="http://schemas.android.com/sdk/android/repo/repository2/03">
  <remotePackage path="emulators;16373346">
    <revision><major>37</major><minor>1</minor><micro>3</micro></revision>
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
  <remotePackage path="emulators;16400000">
    <revision><major>37</major><minor>1</minor><micro>3</micro></revision>
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
</sdk-repository>
)xml";

    ASSERT_OK_AND_ASSIGN(auto latest,
                         FindLatestPackage(kTiebreakerXml, "linux", "x64", "emulators;",
                                           UpdateChannel::kCanary));
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->version.build_id, 16400000);
    EXPECT_EQ(latest->channel, UpdateChannel::kCanary);
}

TEST(RepositoryManifestTest, ParsesSdkPackageVersionAndBuildId) {
    EXPECT_EQ(SdkPackage::Parse("37.1.2", "16173978"),
              (SdkPackage{{37, 1, 2, 16173978}, UpdateChannel::kUnknown}));
    EXPECT_EQ(SdkPackage::Parse("37.1.2.0", "16173978"),
              (SdkPackage{{37, 1, 2, 16173978}, UpdateChannel::kUnknown}));
    EXPECT_EQ(SdkPackage::Parse("37.1.2", "0"), std::nullopt);
    EXPECT_EQ(SdkPackage::Parse("37.1.2", "developer"), std::nullopt);
    EXPECT_EQ(SdkPackage::Parse("37.1.2", "P12345"), std::nullopt);
    EXPECT_EQ(SdkPackage::Parse("37.1", "16173978"), std::nullopt);
    EXPECT_EQ(SdkPackage::Parse("37.1.2.3.4", "16173978"), std::nullopt);
    EXPECT_EQ(SdkPackage::Parse("37.1.2.beta", "16173978"), std::nullopt);
    EXPECT_EQ(SdkPackage::Parse("37.1.2-dev", "16173978"), std::nullopt);
}

TEST(RepositoryManifestTest, PrefixWithoutTrailingSemicolonMatchesBuildIdSuffixOnly) {
    constexpr std::string_view kPrefixXml = R"xml(
<sdk-repository xmlns="http://schemas.android.com/sdk/android/repo/repository2/03">
  <remotePackage path="emulators-extra">
    <revision><major>99</major><minor>0</minor><micro>0</micro></revision>
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
  <remotePackage path="emulators;16373346">
    <revision><major>37</major><minor>1</minor><micro>3</micro></revision>
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
</sdk-repository>
)xml";

    ASSERT_OK_AND_ASSIGN(auto latest,
                         FindLatestPackage(kPrefixXml, "linux", "x64", "emulators",
                                           UpdateChannel::kCanary));
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(*latest, (SdkPackage{{37, 1, 3, 16373346}, UpdateChannel::kCanary}));
}

TEST(RepositoryManifestTest, SkipsIncompleteOrMalformedPackageEntries) {
    constexpr std::string_view kIncompleteEntriesXml = R"xml(
<sdk-repository xmlns="http://schemas.android.com/sdk/android/repo/repository2/03">
  <!-- Missing path attribute -->
  <remotePackage>
    <revision><major>99</major><minor>0</minor><micro>0</micro></revision>
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
  <!-- Missing revision element -->
  <remotePackage path="emulators;17000001">
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
  <!-- Missing micro element -->
  <remotePackage path="emulators;17000002">
    <revision><major>99</major><minor>0</minor></revision>
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
  <!-- Non-numeric revision component -->
  <remotePackage path="emulators;17000003">
    <revision><major>99</major><minor>0</minor><micro>rc1</micro></revision>
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
  <!-- Missing archives element -->
  <remotePackage path="emulators;17000004">
    <revision><major>99</major><minor>0</minor><micro>0</micro></revision>
  </remotePackage>
  <!-- Valid entry with unknown channelRef falls back to canary -->
  <remotePackage path="emulators;16373346">
    <revision><major>37</major><minor>1</minor><micro>3</micro></revision>
    <channelRef ref="unknown-channel"/>
    <archives>
      <metadata/>
      <archive><host-os>linux</host-os></archive>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>
</sdk-repository>
)xml";

    ASSERT_OK_AND_ASSIGN(auto latest,
                         FindLatestPackage(kIncompleteEntriesXml, "linux", "x64", "emulators;",
                                           UpdateChannel::kCanary));
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(*latest, (SdkPackage{{37, 1, 3, 16373346}, UpdateChannel::kCanary}));
}

TEST(RepositoryManifestTest, RejectsInvalidXmlAndWrongNamespace) {
    EXPECT_THAT(FindLatestPackage("", "linux", "x64", "emulators;", UpdateChannel::kCanary),
                StatusIs(absl::StatusCode::kInvalidArgument));

    EXPECT_THAT(FindLatestPackage(kSampleRepositoryXml, "linux", "x64", "", UpdateChannel::kCanary),
                StatusIs(absl::StatusCode::kInvalidArgument));

    EXPECT_THAT(FindLatestPackage("<not-valid-xml", "linux", "x64", "emulators;",
                                  UpdateChannel::kCanary),
                StatusIs(absl::StatusCode::kInvalidArgument));

    EXPECT_THAT(FindLatestPackage("<!-- comment only -->", "linux", "x64", "emulators;",
                                  UpdateChannel::kCanary),
                StatusIs(absl::StatusCode::kInvalidArgument));

    constexpr std::string_view kWrongRootXml = R"xml(
<wrong-root xmlns="http://schemas.android.com/sdk/android/repo/repository2/03">
</wrong-root>
)xml";
    EXPECT_THAT(FindLatestPackage(kWrongRootXml, "linux", "x64", "emulators;",
                                  UpdateChannel::kCanary),
                StatusIs(absl::StatusCode::kInvalidArgument));

    constexpr std::string_view kWrongNamespaceXml = R"xml(
<sdk:sdk-repository xmlns:sdk="http://schemas.android.com/sdk/android/repo/repository2/01">
</sdk:sdk-repository>
)xml";
    EXPECT_THAT(FindLatestPackage(kWrongNamespaceXml, "linux", "x64", "emulators;",
                                  UpdateChannel::kCanary),
                StatusIs(absl::StatusCode::kInvalidArgument));

    constexpr std::string_view kNoNamespaceXml = R"xml(
<sdk-repository>
</sdk-repository>
)xml";
    EXPECT_THAT(FindLatestPackage(kNoNamespaceXml, "linux", "x64", "emulators;",
                                  UpdateChannel::kCanary),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace goldfish::update_check
