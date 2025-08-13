// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "memory_device.h"

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

using testing::Eq;
using testing::Return;
using testing::ReturnRef;

TEST(MemoryDevice, Basic) {
    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;

    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).WillRepeatedly(ReturnRef(hw));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(Eq("-m"), Eq("512")));
}

TEST(MemoryDevice, Default) {
    auto hw = HardwareConfig();
    hw.hw_ramSize = 0;

    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).WillRepeatedly(ReturnRef(hw));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(Eq("-m"), Eq("2048")));
}

TEST(MemoryDevice, Override) {
    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;

    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).WillRepeatedly(ReturnRef(hw));

    AndroidOptions opts{};
    opts.memory = "1024";
    Emulator emu(std::move(avd), std::move(opts));

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(Eq("-m"), Eq("1024")));
}

TEST(MemoryDevice, InvalidOverride) {
    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;

    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).WillRepeatedly(ReturnRef(hw));

    AndroidOptions opts{};
    opts.memory = "foo";
    Emulator emu(std::move(avd), std::move(opts));

    MemoryDevice dev;
    EXPECT_FALSE(dev.initialize(emu).ok());
}

}  // namespace android::goldfish::test
