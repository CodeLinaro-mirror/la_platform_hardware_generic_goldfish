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

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestTempDir.h"
#include "android/cmdline_definitions.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

TEST(Grpc, DefaultPort) {
    EmulatorPorts ports{.serial_number = 5560};
    FakeEmulator emu(std::move(ports), {});

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-device"),
                                     testing::StartsWith("grpc,port=8560,token=true,allowlist="),
                                     testing::Eq("-trace"), testing::Eq("module_*")));
}

TEST(Grpc, CustomPort) {
    AndroidOptions opts{.grpc = "1000"};
    FakeEmulator emu(std::move(opts));

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-device"),
                                     testing::StartsWith("grpc,port=1000,token=true,allowlist="),
                                     testing::Eq("-trace"), testing::Eq("module_*")));
}

TEST(Grpc, DefaultAllowlist) {
    EmulatorPorts ports{.serial_number = 5560};
    FakeEmulator emu(std::move(ports), {});

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(testing::Eq("-device"),
                                 testing::MatchesRegex(absl::StrCat(".*allowlist=.*goldfish\\+", absl::StrReplaceAll(std::filesystem::path("/emulator/launcher/lib/test_allow_list.json").make_preferred().string(), {{"\\", "\\\\"}}), ".*")),
                                 testing::Eq("-trace"), testing::Eq("module_*")));
}

TEST(Grpc, CustomAllowlist) {
    android::base::TestTempDir tmp_dir("Grpc_CustomAllowList");
    std::string allowlist_path = tmp_dir.path().append("allowlist.json").string();
    EXPECT_TRUE(tmp_dir.makeSubFile("allowlist.json"));
    // AndroidOptions requires a non-const char*.
    std::vector<char> allowlist_vec(allowlist_path.begin(), allowlist_path.end());
    allowlist_vec.push_back('\0');

    AndroidOptions opts{.grpc_allowlist = allowlist_vec.data()};
    FakeEmulator emu(std::move(opts));

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-device"),
                                     testing::HasSubstr(std::string("allowlist=") + allowlist_path),
                                     testing::Eq("-trace"), testing::Eq("module_*")));
}

}  // namespace android::goldfish::test
