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

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline-definitions.h"

#include "display_device.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::StartsWith;

TEST(DisplayDeviceTest, GetQemuParameters_VncDisabled) {
    AndroidOptions opts{.enable_vnc = false};
    FakeEmulator emu(std::move(opts));

    DisplayDevice dev("gpu0");
    EXPECT_OK(dev.initialize(emu.config()));

    const auto params = dev.getQemuParameters(emu.config());
    EXPECT_THAT(params,
                ElementsAre(Eq("-display"), Eq("android"), Eq("-device"),
                            Eq("virtio-keyboard-pci,display=gpu0,head=0"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=0"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=1"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=2"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=3"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=4"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=5"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=6"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=7"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=8"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=9"), Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=10")));
}

#if defined(__linux__) || defined(__APPLE__)
TEST(DisplayDeviceTest, GetQemuParameters_VncEnabled) {
    AndroidOptions opts{.enable_vnc = true};
    FakeEmulator emu(std::move(opts));

    DisplayDevice dev("gpu0");
    EXPECT_OK(dev.initialize(emu.config()));

    const auto params = dev.getQemuParameters(emu.config());
    EXPECT_THAT(params,
                ElementsAre(Eq("-display"),
                            StartsWith("vnc=unix:/tmp/.qemu-emu-vnc,display=gpu0,head=0"),
                            Eq("-device"),
                            Eq("virtio-keyboard-pci,display=gpu0,head=0"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=0"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=1"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=2"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=3"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=4"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=5"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=6"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=7"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=8"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=9"),
                            Eq("-device"),
                            Eq("virtio-input-android-pci,display=gpu0,head=10")));
}
#endif

}  // namespace android::goldfish::test
