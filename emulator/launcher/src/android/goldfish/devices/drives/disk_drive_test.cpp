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
#include "disk_drive.h"

#include <android/base/system/System.h>
#include <android/goldfish/config/hardware_config.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "absl/log/globals.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestSystem.h"
#include "android/cmdline-definitions.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/devices/mock_avd.h"

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

namespace android::goldfish::test {

TEST(RawDrive, Basic_x86) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    auto hw = HardwareConfig();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();

    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::INITSYSTEM))
            .Times(2).WillRepeatedly(testing::Return("some/path/disk-image"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    RawDrive dev("system", "abc", Avd::ImageType::INITSYSTEM);
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(
            dev.getQemuParameters(emu),
            testing::ElementsAre(testing::Eq("-device"),
                                 testing::Eq("virtio-blk-pci,addr=abc,drive=disk-image,num-queues="
                                             "4,iothread=disk-iothread"),
                                 testing::Eq("-blockdev"),
                                 testing::Eq("driver=raw,node-name=disk-image,read-only=on,"
                                             "driver=file,filename=some/path/disk-image")));
}

TEST(RawDrive, Basic_arm64) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    auto hw = HardwareConfig();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();

    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::INITSYSTEM))
            .Times(2).WillRepeatedly(testing::Return("some/path/disk-image"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    RawDrive dev("system", "ignored", Avd::ImageType::INITSYSTEM);
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-device"),
                                     testing::Eq("virtio-blk-device,drive=disk-image,num-queues=4,"
                                                 "iothread=disk-iothread"),
                                     testing::Eq("-blockdev"),
                                     testing::Eq("driver=raw,node-name=disk-image,read-only=on,"
                                                 "driver=file,filename=some/path/"
                                                 "disk-image")));
}

TEST(RawDrive, MissingImage) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    auto hw = HardwareConfig();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();

    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::INITSYSTEM))
            .WillOnce(testing::Return(absl::NotFoundError("not found")));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    RawDrive dev("system", "abc", Avd::ImageType::INITSYSTEM);
    EXPECT_THAT(dev.initialize(emu), StatusIs(absl::StatusCode::kNotFound));
}


}  // namespace android::goldfish::test
