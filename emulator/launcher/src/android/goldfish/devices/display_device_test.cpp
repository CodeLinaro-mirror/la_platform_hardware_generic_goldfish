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
#include "display_device.h"

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

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::StartsWith;

TEST(DisplayDeviceTest, GetQemuParameters_VncDisabled) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{};
    opts.enable_vnc = false;
    Emulator emu({}, std::move(avd), std::move(opts));

    DisplayDevice dev("gpu0");
    EXPECT_OK(dev.initialize(emu));

    const auto params = dev.getQemuParameters(emu);
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
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{.enable_vnc = true};
    Emulator emu({}, std::move(avd), std::move(opts));

    DisplayDevice dev("gpu0");
    EXPECT_OK(dev.initialize(emu));

    const auto params = dev.getQemuParameters(emu);
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
