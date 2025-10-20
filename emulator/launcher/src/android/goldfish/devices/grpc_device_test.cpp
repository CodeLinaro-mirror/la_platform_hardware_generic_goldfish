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

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline-definitions.h"

#include "grpc_device.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

TEST(Grpc, DefaultPort) {
    FakeEmulator emu;

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-device"),
                                     testing::StartsWith("grpc,port=8556,token=true,allowlist="),
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

}  // namespace android::goldfish::test
