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

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline_definitions.h"
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

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), testing::ElementsAre(Eq("-m"), Eq("512")));
}

TEST(MemoryDevice, Default) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_ramSize = 0;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

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

    MemoryDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), testing::ElementsAre(Eq("-m"), Eq("1024")));
}

TEST(MemoryDevice, InvalidOverride) {
    AndroidOptions opts{.memory = "foo"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    hw.hw_ramSize = 512;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

    MemoryDevice dev;
    EXPECT_FALSE(dev.initialize(emu.config()).ok());
}

}  // namespace android::goldfish::test
