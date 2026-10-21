// Copyright 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "grpc_device.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"

#include "android/base/testing/test_temp_dir.h"
#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

TEST(Grpc, DefaultPort) {
    EmulatorPorts ports{.serial_number = 5560};
    FakeEmulator emu(std::move(ports), {});

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-device"),
                        testing::StartsWith("grpc,port=8560,token=false,jwt=false,allowlist=")));
}

TEST(Grpc, CustomPort) {
    AndroidOptions opts{.grpc = "1000"};
    FakeEmulator emu(std::move(opts));

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-device"),
                        testing::StartsWith("grpc,port=1000,token=false,jwt=false,allowlist=")));
}

TEST(Grpc, CustomPortWithToken) {
    AndroidOptions opts{.grpc = "1000", .grpc_use_token = true};
    FakeEmulator emu(std::move(opts));

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-device"),
                        testing::StartsWith("grpc,port=1000,token=true,jwt=false,allowlist=")));
}

TEST(Grpc, CustomPortWithJwt) {
    AndroidOptions opts{.grpc = "1000", .grpc_use_jwt = true};
    FakeEmulator emu(std::move(opts));

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-device"),
                        testing::StartsWith("grpc,port=1000,token=false,jwt=true,allowlist=")));
}

TEST(Grpc, DefaultAllowlist) {
    EmulatorPorts ports{.serial_number = 5560};
    FakeEmulator emu(std::move(ports), {});

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-device"),
                        testing::MatchesRegex(absl::StrCat(
                                ".*allowlist=.*goldfish\\+",
                                absl::StrReplaceAll(
                                        std::filesystem::path(
                                                "/emulator/launcher/lib/emulator_access.json")
                                                .make_preferred()
                                                .string(),
                                        {{"\\", "\\\\"}}),
                                ".*"))));
}

TEST(Grpc, CustomAllowlist) {
    android::base::TestTempDir tmp_dir("Grpc_CustomAllowList");
    std::string allowlist_path = tmp_dir.Path().append("allowlist.json").string();
    EXPECT_TRUE(tmp_dir.MakeSubFile("allowlist.json"));
    // AndroidOptions requires a non-const char*.
    std::vector<char> allowlist_vec(allowlist_path.begin(), allowlist_path.end());
    allowlist_vec.push_back('\0');

    AndroidOptions opts{.grpc_allowlist = allowlist_vec.data()};
    FakeEmulator emu(std::move(opts));

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(testing::Eq("-device"),
                                 testing::HasSubstr(std::string("allowlist=") + allowlist_path)));
}

TEST(Grpc, EscapesCommasInPaths) {
    android::base::TestTempDir tmp_dir("Grpc_EscapesCommasInPaths");
    EXPECT_TRUE(tmp_dir.MakeSubDir("dir,with,comma"));
    EXPECT_TRUE(tmp_dir.MakeSubFile("dir,with,comma/allow,list.json"));
    std::string allowlist_path = (tmp_dir.Path() / "dir,with,comma" / "allow,list.json").string();
    std::vector<char> allowlist_vec(allowlist_path.begin(), allowlist_path.end());
    allowlist_vec.push_back('\0');

    AndroidOptions opts{.grpc_allowlist = allowlist_vec.data()};
    FakeEmulator emu(std::move(opts));
    emu.user_paths().discovery_directory = "/home/Doe, Jane/.android/avd/running";
    emu.emulator_paths().launcher_directory = "/opt/Android, SDK/emulator";

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(
                    testing::Eq("-device"),
                    testing::AllOf(
                            testing::HasSubstr("allowlist=" +
                                               Device::EscapeQemuOptionValue(allowlist_path)),
                            testing::HasSubstr(
                                    "discovery_dir=" +
                                    Device::EscapeQemuOptionValue(
                                            emu.user_paths().discovery_directory.string())),
                            testing::HasSubstr(
                                    "launcher_dir=" +
                                    Device::EscapeQemuOptionValue(
                                            emu.emulator_paths().launcher_directory.string())))));
}

}  // namespace android::goldfish::test
