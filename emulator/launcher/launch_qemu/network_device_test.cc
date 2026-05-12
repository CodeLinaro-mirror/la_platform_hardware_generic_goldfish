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

#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

TEST(Network, Basic_x86) {
    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    NetworkDevice dev("eth0", "0a.0", /*cellular=*/false, /*netsim_backend=*/true);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-device"),
                                     testing::Eq("netsim-netdev,id=eth0,mode=ethernet"),
                                     testing::Eq("-device"),
                                     testing::Eq("virtio-net-pci,addr=0a.0,netdev=eth0")));
}

TEST(Network, Basic_arm64) {
    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    NetworkDevice dev("eth0", "0a.0", /*cellular=*/true, /*netsim_backend=*/true);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-device"), testing::Eq("netsim-netdev,id=eth0,mode=cellular"),
                        testing::Eq("-device"), testing::Eq("virtio-net-device,netdev=eth0")));
}

}  // namespace android::goldfish::test
