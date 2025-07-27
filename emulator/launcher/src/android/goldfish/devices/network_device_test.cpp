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
#include "network_device.h"

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

TEST(Network, Basic_x86) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    // First the 3 calls by Emulator ctor.
    EXPECT_CALL(*avd_ptr, name()).WillOnce(testing::Return("mock_avd"));
    EXPECT_CALL(*avd_ptr, hw()).WillOnce(testing::ReturnRef(hw));
    EXPECT_CALL(*avd_ptr, getIniFile()).WillOnce(testing::Return("some/path/mock_avd.ini"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    NetworkDevice dev("0a.0");
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-netdev"), testing::Eq("hubport,id=mynet,hubid=1234"),
                                     testing::Eq("-device"), testing::Eq("virtio-net-pci,addr=0a.0,netdev=mynet")));
}

TEST(Network, Basic_arm64) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    // First the 3 calls by Emulator ctor.
    EXPECT_CALL(*avd_ptr, name()).WillOnce(testing::Return("mock_avd"));
    EXPECT_CALL(*avd_ptr, hw()).WillOnce(testing::ReturnRef(hw));
    EXPECT_CALL(*avd_ptr, getIniFile()).WillOnce(testing::Return("some/path/mock_avd.ini"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    NetworkDevice dev("0a.0");
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-netdev"), testing::Eq("hubport,id=mynet,hubid=1234"),
                                     testing::Eq("-device"), testing::Eq("virtio-net-device,netdev=mynet")));
}

}  // namespace android::goldfish::test
