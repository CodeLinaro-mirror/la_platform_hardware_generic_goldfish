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

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using testing::Eq;
using testing::Return;
using testing::ReturnRef;

TEST(MemoryDevice, Basic) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(21));

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), testing::ElementsAre(Eq("-m"), Eq("1024")));
}

TEST(MemoryDevice, Default) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_ramSize = 0;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(30));

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), testing::ElementsAre(Eq("-m"), Eq("2048")));
}

TEST(MemoryDevice, Override) {
    AndroidOptions opts{.memory = "1024"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(21));

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), testing::ElementsAre(Eq("-m"), Eq("1024")));
}

TEST(MemoryDevice, Api37Minimum) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(37));

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), testing::ElementsAre(Eq("-m"), Eq("4096")));
}

TEST(MemoryDevice, LowRam) {
    AndroidOptions opts{.lowram = 1};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(37));

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), testing::ElementsAre(Eq("-m"), Eq("512")));
}

TEST(MemoryDevice, InvalidOverride) {
    AndroidOptions opts{.memory = "foo"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(21));

    MemoryDevice dev;
    EXPECT_FALSE(dev.initialize(emu.config()).ok());
}

}  // namespace android::goldfish::test
