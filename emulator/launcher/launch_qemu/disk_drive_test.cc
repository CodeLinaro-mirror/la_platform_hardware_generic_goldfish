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

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

#include "android/base/testing/test_system.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

namespace android::goldfish::test {

TEST(RoDrive, Basic_x86) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);
    auto image_file = launcher_path / "disk-image";

    std::ofstream{image_file};

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    RoDrive dev("system", "03.0", image_file);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(
                    testing::Eq("-device"), testing::Eq("virtio-blk-pci,addr=03.0,drive=system"),
                    testing::Eq("-blockdev"),
                    testing::Eq(absl::StrCat("driver=raw,node-name=system,read-only=on,"
                                             "file.driver=file,file.filename=",
                                             image_file.string()))));
}

TEST(RoDrive, Basic_arm64) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);
    auto image_file = launcher_path / "disk-image";

    // TODO(whollins): clean-up created files and directories.
    std::ofstream{image_file};

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    RoDrive dev("system", "03.0", image_file);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-device"), testing::Eq("virtio-blk-device,drive=system"),
                        testing::Eq("-blockdev"),
                        testing::Eq(absl::StrCat("driver=raw,node-name=system,read-only=on,"
                                                 "file.driver=file,file.filename=",
                                                 image_file.string()))));
}

TEST(RoDrive, MissingImage) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);
    auto image_file = launcher_path / "disk-image-not-found";

    FakeEmulator emu;

    RoDrive dev("system", "03.0", image_file);
    EXPECT_THAT(dev.initialize(emu.config()), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RwDrive, Basic_x86) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);
    auto userData = launcher_path / "userdata.img";
    auto qcow2Image = launcher_path / "userdata-qemu.qcow2";
    std::ofstream(userData).close();
    std::ofstream(qcow2Image).close();

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    RwDrive dev("userdata", "04.0", std::nullopt, userData, qcow2Image, 1024);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(
                    "-device", "virtio-blk-pci,addr=04.0,drive=userdata,write-cache=on",
                    "-blockdev",
                    absl::StrCat("driver=qcow2,node-name=userdata,file.driver=file,file.filename=",
                                 qcow2Image.string(),
                                 ",overlap-check=none,cache.direct=off,cache.no-flush=on,l2-"
                                 "cache-size=1048576")));
}

TEST(RwDrive, Basic_arm) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);
    auto userData = launcher_path / "userdata.img";
    auto qcow2Image = launcher_path / "userdata-qemu.qcow2";
    std::ofstream(userData).close();
    std::ofstream(qcow2Image).close();

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    RwDrive dev("userdata", "04.0", std::nullopt, userData, qcow2Image, 1024);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(
                    "-device", "virtio-blk-device,drive=userdata,write-cache=on", "-blockdev",
                    absl::StrCat("driver=qcow2,node-name=userdata,file.driver=file,file.filename=",
                                 qcow2Image.string(),
                                 ",overlap-check=none,cache.direct=off,cache.no-flush=on,l2-"
                                 "cache-size=1048576")));
}

}  // namespace android::goldfish::test
