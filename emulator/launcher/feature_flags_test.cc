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
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"

#include "android/base/bazel_info.h"
#include "android/base/testing/test_system.h"
#include "android/status/status_matcher_macros.h"

namespace android::goldfish {
namespace {

namespace fs = std::filesystem;
using S = android_studio::EmulatorFeatureFlagState;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

void WriteTextFile(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

TEST(FeatureFlagsTest, ParseFeatureFileSkipsLegacyAndHandlesSpecialNamesViaCreate) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path host_ini = dir / "host.ini";
    const fs::path guest_ini = dir / "guest.ini";

    WriteTextFile(host_ini,
                  "# Test advanced features file\n"
                  "mac80211hwsimUserspaceManaged=off\n"
                  "mac80211hwsimUserspaceManaged=on\n"
                  "Minigbm = ON\n"
                  "ForceLavapipe = OFF\n"
                  "GuestAngle = default\n"
                  "Uwb = foo\n"
                  "QtRawKeyboardInput = on\n"
                  "VsockSnapshotLoadFixed_b231345789 = on\n"
                  "HostComposition = on\n"
                  "GLPipeChecksum = on\n"
                  "GrallocSync = on\n"
                  "VulkanNativeSwapchain = on\n"
                  "HVF = on\n"
                  "WindowsHypervisorPlatform = on\n"
                  "RefCountPipe\n");
    WriteTextFile(guest_ini,
                  "mac80211hwsimUserspaceManaged = on\n"
                  "Minigbm = on\n"
                  "GuestAngle = on\n"
                  "Uwb = on\n"
                  "VsockSnapshotLoadFixed_b231345789 = on\n"
                  "someUnknownGuestFeature = off\n"
                  "I did something = wrong =h ere\n");

    const std::vector<std::string> cli_flags = {
        "VulkanNativeSwapchain,-VulkanNativeSwapchain,HVF,WindowsHypervisorPlatform",
    };
    ASSERT_OK_AND_ASSIGN(const FeatureFlags flags, FeatureFlags::Create({
                                                       .host_ini_path = host_ini,
                                                       .guest_ini_path = guest_ini,
                                                       .cli_feature_flags = cli_flags,
                                                   }));

    // Duplicate key takes last value ('on').
    EXPECT_TRUE(flags.IsEnabled(S::MAC80211HWSIM_USERSPACE_MANAGED));
    EXPECT_TRUE(flags.IsEnabled(S::MINIGBM));
    EXPECT_FALSE(flags.IsEnabled(S::FORCE_LAVAPIPE));
    // 'default' and 'foo' in host INI are not 'on'.
    EXPECT_FALSE(flags.IsEnabled(S::GUEST_ANGLE));
    EXPECT_FALSE(flags.IsEnabled(S::UWB));

    // Special INI key names map to their enum values.
    EXPECT_TRUE(flags.IsEnabled(S::DEVICE_KEYBOARD_QT_RAW_INPUT));
    EXPECT_TRUE(flags.IsEnabled(S::VSOCK_SNAPSHOT_LOAD_FIXED_B231345789));

    // Legacy flags are recognized without error, ignored in INI and CLI overrides,
    // and never reported as enabled or overridden.
    for (auto legacy : {S::HOST_COMPOSITION_V1, S::GL_PIPE_CHECKSUM, S::GRALLOC_SYNC,
                        S::VULKAN_NATIVE_SWAPCHAIN, S::HVF, S::WINDOWS_HYPERVISOR_PLATFORM}) {
        EXPECT_FALSE(flags.IsEnabled(legacy));
        EXPECT_FALSE(flags.IsOverridden(legacy));
    }
}

TEST(FeatureFlagsTest, HostAndGuestBaselineEvaluation) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path host_ini = dir / "host_advancedFeatures.ini";
    const fs::path guest_ini = dir / "guest_advancedFeatures.ini";

    WriteTextFile(host_ini,
                  "NetsimWebUi = on\n"
                  "ForceLavapipe = off\n"
                  "Minigbm = on\n"
                  "GuestAngle = on\n"
                  "Uwb = off\n");
    WriteTextFile(guest_ini,
                  "Minigbm = on\n"
                  "GuestAngle = off\n"
                  "Uwb = on\n");

    ASSERT_OK_AND_ASSIGN(const FeatureFlags flags, FeatureFlags::Create({
                                                       .host_ini_path = host_ini,
                                                       .guest_ini_path = guest_ini,
                                                   }));

    // Host-only features depend solely on host INI.
    EXPECT_TRUE(flags.IsEnabled(S::NETSIMWEBUI));
    EXPECT_FALSE(flags.IsEnabled(S::FORCE_LAVAPIPE));

    // Guest features require BOTH host INI and guest INI to be 'on'.
    EXPECT_TRUE(flags.IsEnabled(S::MINIGBM));
    EXPECT_TRUE(flags.IsEnabledByGuest(S::MINIGBM));

    EXPECT_FALSE(flags.IsEnabled(S::GUEST_ANGLE));
    EXPECT_FALSE(flags.IsEnabledByGuest(S::GUEST_ANGLE));

    EXPECT_FALSE(flags.IsEnabled(S::UWB));
    EXPECT_TRUE(flags.IsEnabledByGuest(S::UWB));

    EXPECT_THAT(flags.GetEnabled(), ElementsAre(S::MINIGBM, S::NETSIMWEBUI));
    EXPECT_THAT(flags.GetEnabledNonOverride(), ElementsAre(S::MINIGBM, S::NETSIMWEBUI));
    EXPECT_THAT(flags.GetEnabledOverride(), IsEmpty());
    EXPECT_THAT(flags.GetDisabledOverride(), IsEmpty());
}

TEST(FeatureFlagsTest, MissingIniFileReturnsNotFoundError) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path host_ini = dir / "host_advancedFeatures.ini";

    WriteTextFile(host_ini,
                  "NetsimWebUi = on\n"
                  "Minigbm = on\n");

    auto missing_guest = FeatureFlags::Create({
        .host_ini_path = host_ini,
        .guest_ini_path = dir / "missing_guest.ini",
    });
    EXPECT_FALSE(missing_guest.ok());
    EXPECT_EQ(missing_guest.status().code(), absl::StatusCode::kNotFound);

    auto missing_host = FeatureFlags::Create({
        .host_ini_path = dir / "missing_host.ini",
    });
    EXPECT_FALSE(missing_host.ok());
    EXPECT_EQ(missing_host.status().code(), absl::StatusCode::kNotFound);

    // Omitting guest_ini_path (empty path) succeeds and disables guest features.
    ASSERT_OK_AND_ASSIGN(const FeatureFlags flags, FeatureFlags::Create({
                                                       .host_ini_path = host_ini,
                                                   }));
    EXPECT_TRUE(flags.IsEnabled(S::NETSIMWEBUI));
    EXPECT_FALSE(flags.IsEnabled(S::MINIGBM));
    EXPECT_FALSE(flags.IsEnabledByGuest(S::MINIGBM));
}

TEST(FeatureFlagsTest, OverridePrecedenceOrder) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path host_ini = dir / "host.ini";
    const fs::path guest_ini = dir / "guest.ini";
    const fs::path user_ini = dir / "user.ini";

    WriteTextFile(host_ini,
                  "NetsimWebUi = on\n"
                  "NetsimCliUi = off\n"
                  "ForceLavapipe = off\n"
                  "Minigbm = on\n"
                  "GuestAngle = on\n");
    WriteTextFile(guest_ini,
                  "Minigbm = on\n"
                  "GuestAngle = on\n");

    // User INI overrides NetsimWebUi -> off, NetsimCliUi -> on, ForceLavapipe -> on.
    WriteTextFile(user_ini,
                  "NetsimWebUi = off\n"
                  "NetsimCliUi = on\n"
                  "ForceLavapipe = on\n");

    // AVD config overrides NetsimCliUi -> off, ForceLavapipe -> off.
    const std::string_view avd_flags = "-NetsimCliUi,-ForceLavapipe";

    // Env var overrides ForceLavapipe -> on, Minigbm -> off.
    const std::string_view env_flags = "ForceLavapipe,-Minigbm";

    // CLI overrides Minigbm -> on, GuestAngle -> off.
    const std::vector<std::string> cli_flags = {"Minigbm,-GuestAngle"};

    ASSERT_OK_AND_ASSIGN(const FeatureFlags flags, FeatureFlags::Create({
                                                       .host_ini_path = host_ini,
                                                       .guest_ini_path = guest_ini,
                                                       .user_ini_path = user_ini,
                                                       .avd_feature_flags = avd_flags,
                                                       .env_feature_flags = env_flags,
                                                       .cli_feature_flags = cli_flags,
                                                   }));

    EXPECT_FALSE(flags.IsEnabled(S::NETSIMWEBUI));
    EXPECT_TRUE(flags.IsOverridden(S::NETSIMWEBUI));

    EXPECT_FALSE(flags.IsEnabled(S::NETSIMCLIUI));
    EXPECT_TRUE(flags.IsOverridden(S::NETSIMCLIUI));

    EXPECT_TRUE(flags.IsEnabled(S::FORCE_LAVAPIPE));
    EXPECT_TRUE(flags.IsOverridden(S::FORCE_LAVAPIPE));

    EXPECT_TRUE(flags.IsEnabled(S::MINIGBM));
    EXPECT_TRUE(flags.IsOverridden(S::MINIGBM));

    EXPECT_FALSE(flags.IsEnabled(S::GUEST_ANGLE));
    EXPECT_TRUE(flags.IsOverridden(S::GUEST_ANGLE));

    EXPECT_THAT(flags.GetEnabledNonOverride(),
                ElementsAre(S::MINIGBM, S::NETSIMWEBUI, S::GUEST_ANGLE));
    EXPECT_THAT(flags.GetEnabledOverride(), ElementsAre(S::MINIGBM, S::FORCE_LAVAPIPE));
    EXPECT_THAT(flags.GetDisabledOverride(),
                ElementsAre(S::NETSIMWEBUI, S::NETSIMCLIUI, S::GUEST_ANGLE));
    EXPECT_THAT(flags.GetEnabled(), ElementsAre(S::MINIGBM, S::FORCE_LAVAPIPE));
}

TEST(FeatureFlagsTest, UserIniDefaultResetsToBaseline) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path host_ini = dir / "host.ini";
    const fs::path user_ini = dir / "user.ini";

    WriteTextFile(host_ini, "NetsimWebUi = on\n");
    WriteTextFile(user_ini, "NetsimWebUi = default\n");

    ASSERT_OK_AND_ASSIGN(const FeatureFlags flags, FeatureFlags::Create({
                                                       .host_ini_path = host_ini,
                                                       .user_ini_path = user_ini,
                                                   }));

    EXPECT_TRUE(flags.IsEnabled(S::NETSIMWEBUI));
    EXPECT_FALSE(flags.IsOverridden(S::NETSIMWEBUI));
}

TEST(FeatureFlagsTest, SetIfNotOverridden) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path host_ini = dir / "host.ini";
    const fs::path guest_ini = dir / "guest.ini";

    WriteTextFile(host_ini,
                  "ForceSwiftshader = off\n"
                  "ForceLavapipe = off\n"
                  "Minigbm = off\n"
                  "GuestAngle = off\n");
    WriteTextFile(guest_ini,
                  "Minigbm = on\n"
                  "GuestAngle = off\n");

    ASSERT_OK_AND_ASSIGN(FeatureFlags flags, FeatureFlags::Create({
                                                 .host_ini_path = host_ini,
                                                 .guest_ini_path = guest_ini,
                                                 .avd_feature_flags = "-ForceLavapipe",
                                             }));

    // Feature not overridden -> SetIfNotOverridden updates it without marking overridden.
    flags.SetIfNotOverridden(S::FORCE_SWIFTSHADER, true);
    EXPECT_TRUE(flags.IsEnabled(S::FORCE_SWIFTSHADER));
    EXPECT_FALSE(flags.IsOverridden(S::FORCE_SWIFTSHADER));

    // Feature overridden by user/AVD -> SetIfNotOverridden must not change it.
    flags.SetIfNotOverridden(S::FORCE_LAVAPIPE, true);
    EXPECT_FALSE(flags.IsEnabled(S::FORCE_LAVAPIPE));

    // Guest feature enabled by guest -> SetIfNotOverridden can enable it.
    flags.SetIfNotOverridden(S::MINIGBM, true);
    EXPECT_TRUE(flags.IsEnabled(S::MINIGBM));
    EXPECT_FALSE(flags.IsOverridden(S::MINIGBM));

    // Guest feature disabled by guest -> SetIfNotOverridden must not enable it.
    flags.SetIfNotOverridden(S::GUEST_ANGLE, true);
    EXPECT_FALSE(flags.IsEnabled(S::GUEST_ANGLE));
}

TEST(FeatureFlagsTest, CreateForTestingSetsBaselineWithoutMarkingOverridden) {
    auto flags = FeatureFlags::CreateForTesting({S::MINIGBM, S::VULKAN_SNAPSHOTS});

    EXPECT_TRUE(flags.IsEnabled(S::MINIGBM));
    // CreateForTesting must not mutate the guest image capability state.
    EXPECT_FALSE(flags.IsEnabledByGuest(S::MINIGBM));
    EXPECT_FALSE(flags.IsOverridden(S::MINIGBM));
    EXPECT_TRUE(flags.IsEnabled(S::VULKAN_SNAPSHOTS));
    EXPECT_FALSE(flags.IsOverridden(S::VULKAN_SNAPSHOTS));
    EXPECT_THAT(flags.GetEnabledOverride(), IsEmpty());
    EXPECT_THAT(flags.GetEnabledNonOverride(), ElementsAre(S::VULKAN_SNAPSHOTS, S::MINIGBM));

    // Overrides take precedence over the initial baseline while preserving baseline reporting.
    flags.SetEnabledOverride(S::MINIGBM, false);
    EXPECT_FALSE(flags.IsEnabled(S::MINIGBM));
    EXPECT_TRUE(flags.IsOverridden(S::MINIGBM));
    EXPECT_THAT(flags.GetEnabledNonOverride(), ElementsAre(S::VULKAN_SNAPSHOTS, S::MINIGBM));
    EXPECT_THAT(flags.GetDisabledOverride(), ElementsAre(S::MINIGBM));
}

TEST(FeatureFlagsTest, AllEnumFeaturesRoundTripBetweenNameAndFeatureLookup) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path host_ini = dir / "host.ini";
    const fs::path guest_ini = dir / "guest.ini";

    for (int i = S::EmulatorFeatureFlag_MIN; i <= S::EmulatorFeatureFlag_MAX; ++i) {
        if (!S::EmulatorFeatureFlag_IsValid(i)) {
            continue;
        }
        const auto feature = static_cast<EmulatorFeatureFlag>(i);
        if (feature == S::EMULATOR_FEATURE_FLAG_UNSPECIFIED) {
            continue;
        }

        const std::string& enum_name = S::EmulatorFeatureFlag_Name(feature);
        std::string stripped_lower;
        for (char c : enum_name) {
            if (c != '_') {
                stripped_lower.push_back(
                        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }

        for (std::string_view candidate :
             {std::string_view(enum_name), std::string_view(stripped_lower)}) {
            const std::string ini_line = std::string(candidate) + " = on\n";
            WriteTextFile(host_ini, ini_line);
            WriteTextFile(guest_ini, ini_line);

            ASSERT_OK_AND_ASSIGN(const FeatureFlags flags, FeatureFlags::Create({
                                                               .host_ini_path = host_ini,
                                                               .guest_ini_path = guest_ini,
                                                               .avd_feature_flags = candidate,
                                                           }));
            if (!flags.GetEnabled().empty()) {
                EXPECT_THAT(flags.GetEnabled(), ElementsAre(feature)) << "candidate=" << candidate;
                EXPECT_THAT(flags.GetEnabledOverride(), ElementsAre(feature))
                        << "candidate=" << candidate;
            }
        }
    }
}

TEST(FeatureFlagsTest, AllHostIniFeaturesAreMappedToEnum) {
    const fs::path ini_path = android::base::Bazel::RunfilesPath(
            "goldfish+/emulator/launcher/data/advancedFeatures.ini");
    ASSERT_OK_AND_ASSIGN(const FeatureFlags flags, FeatureFlags::Create({
                                                       .host_ini_path = ini_path,
                                                   }));
    EXPECT_FALSE(flags.GetEnabled().empty());
}

TEST(FeatureFlagsTest, UnknownFeatureInHostIniReturnsErrorWhileGuestIniSucceeds) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path bad_host_ini = dir / "bad_host.ini";
    const fs::path good_host_ini = dir / "good_host.ini";
    const fs::path guest_ini = dir / "guest.ini";

    WriteTextFile(bad_host_ini,
                  "NetsimWebUi = on\n"
                  "UnrecognizedHostFeature = on\n");
    WriteTextFile(good_host_ini,
                  "NetsimWebUi = on\n"
                  "Minigbm = on\n");
    WriteTextFile(guest_ini,
                  "Minigbm = on\n"
                  "UnrecognizedGuestFeature = on\n");

    auto bad_host = FeatureFlags::Create({
        .host_ini_path = bad_host_ini,
    });
    EXPECT_FALSE(bad_host.ok());
    EXPECT_EQ(bad_host.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(bad_host.status().message(), ::testing::HasSubstr(bad_host_ini.string()));
    EXPECT_THAT(bad_host.status().message(), ::testing::HasSubstr("UnrecognizedHostFeature"));

    ASSERT_OK_AND_ASSIGN(const FeatureFlags flags, FeatureFlags::Create({
                                                       .host_ini_path = good_host_ini,
                                                       .guest_ini_path = guest_ini,
                                                   }));
    EXPECT_TRUE(flags.IsEnabled(S::NETSIMWEBUI));
    EXPECT_TRUE(flags.IsEnabled(S::MINIGBM));
}

TEST(FeatureFlagsTest, Api37OrGreaterPatchesMinigbmAndGuestAngleInGuestFeatures) {
    base::TestSystem sys("bin", "myhome");
    const fs::path dir = sys.GetTempRoot()->Path();
    const fs::path host_ini = dir / "host.ini";
    const fs::path guest_ini = dir / "guest.ini";

    WriteTextFile(host_ini,
                  "Minigbm = on\n"
                  "GuestAngle = on\n"
                  "DynamicPartition = on\n");
    // System image INI omits Minigbm and GuestAngle.
    WriteTextFile(guest_ini, "DynamicPartition = on\n");

    // Below API 37, omitted guest features remain disabled.
    ASSERT_OK_AND_ASSIGN(const FeatureFlags api36_flags, FeatureFlags::Create({
                                                             .host_ini_path = host_ini,
                                                             .guest_ini_path = guest_ini,
                                                             .guest_api_level = 36,
                                                         }));
    EXPECT_FALSE(api36_flags.IsEnabledByGuest(S::MINIGBM));
    EXPECT_FALSE(api36_flags.IsEnabled(S::MINIGBM));
    EXPECT_FALSE(api36_flags.IsEnabledByGuest(S::GUEST_ANGLE));
    EXPECT_FALSE(api36_flags.IsEnabled(S::GUEST_ANGLE));

    // At API 37+, Minigbm and GuestAngle are patched to On in guest features.
    ASSERT_OK_AND_ASSIGN(const FeatureFlags api37_flags, FeatureFlags::Create({
                                                             .host_ini_path = host_ini,
                                                             .guest_ini_path = guest_ini,
                                                             .guest_api_level = 37,
                                                         }));
    EXPECT_TRUE(api37_flags.IsEnabledByGuest(S::MINIGBM));
    EXPECT_TRUE(api37_flags.IsEnabled(S::MINIGBM));
    EXPECT_TRUE(api37_flags.IsEnabledByGuest(S::GUEST_ANGLE));
    EXPECT_TRUE(api37_flags.IsEnabled(S::GUEST_ANGLE));

    // Explicit user/CLI overrides can still disable them on API 37+.
    const std::vector<std::string> cli_flags = {"-GuestAngle,+ForceLavapipe"};
    ASSERT_OK_AND_ASSIGN(const FeatureFlags overridden_flags, FeatureFlags::Create({
                                                                  .host_ini_path = host_ini,
                                                                  .guest_ini_path = guest_ini,
                                                                  .guest_api_level = 37,
                                                                  .cli_feature_flags = cli_flags,
                                                              }));
    EXPECT_TRUE(overridden_flags.IsEnabledByGuest(S::GUEST_ANGLE));
    EXPECT_FALSE(overridden_flags.IsEnabled(S::GUEST_ANGLE));
    EXPECT_TRUE(overridden_flags.IsOverridden(S::GUEST_ANGLE));
    EXPECT_TRUE(overridden_flags.IsEnabled(S::FORCE_LAVAPIPE));
    EXPECT_TRUE(overridden_flags.IsOverridden(S::FORCE_LAVAPIPE));

    // Explicit 'off' in the guest INI on API 37+ is respected (not overwritten by try_emplace).
    const fs::path explicit_off_guest_ini = dir / "explicit_off_guest.ini";
    WriteTextFile(explicit_off_guest_ini,
                  "DynamicPartition = on\n"
                  "GuestAngle = off\n");
    ASSERT_OK_AND_ASSIGN(const FeatureFlags explicit_off_flags,
                         FeatureFlags::Create({
                             .host_ini_path = host_ini,
                             .guest_ini_path = explicit_off_guest_ini,
                             .guest_api_level = 37,
                         }));
    EXPECT_TRUE(explicit_off_flags.IsEnabledByGuest(S::MINIGBM));
    EXPECT_TRUE(explicit_off_flags.IsEnabled(S::MINIGBM));
    EXPECT_FALSE(explicit_off_flags.IsEnabledByGuest(S::GUEST_ANGLE));
    EXPECT_FALSE(explicit_off_flags.IsEnabled(S::GUEST_ANGLE));
}

}  // namespace
}  // namespace android::goldfish
