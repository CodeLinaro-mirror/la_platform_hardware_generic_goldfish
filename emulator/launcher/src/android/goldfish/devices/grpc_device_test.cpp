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

#include <android/base/system/System.h>
#include <android/goldfish/config/hardware_config.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/log/globals.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestSystem.h"
#include "android/cmdline-definitions.h"
#include "android/goldfish/config/emulator.h"
#include "mock_avd.h"

namespace android::goldfish::test {

TEST(Grpc, DefaultPort) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{};
    Emulator emu({}, std::move(avd), std::move(opts));

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-device"),
                                     testing::StartsWith("grpc,port=8556,token=true,allowlist="),
                                     testing::Eq("-trace"), testing::Eq("module_*")));
}

TEST(Grpc, CustomPort) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{
        .grpc = "1000",
    };
    Emulator emu({}, std::move(avd), std::move(opts));

    GrpcDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-device"),
                                     testing::StartsWith("grpc,port=1000,token=true,allowlist="),
                                     testing::Eq("-trace"), testing::Eq("module_*")));
}

}  // namespace android::goldfish::test
