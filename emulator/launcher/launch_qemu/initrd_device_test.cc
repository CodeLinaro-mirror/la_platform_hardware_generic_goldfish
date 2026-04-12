// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "initrd_device.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>

#include "gmock/gmock.h"

#include "android/status/status_matcher_macros.h"
#include "android/cmdline_definitions.h"
#include "android/base/testing/TestSystem.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

TEST(BootProperties, Basic) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    FakeEmulator emu;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    EXPECT_THAT(getBootProperties(emu.config()), testing::IsSupersetOf(std::vector{
                                                     std::pair{"androidboot.hardware", "ranchu"},
                                                     std::pair{"androidboot.logcat", "*:V"},
                                                 }));
}

TEST(BootProperties, NoBootAnim) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    AndroidOptions opts{.no_boot_anim = true};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    EXPECT_THAT(getBootProperties(emu.config()),
                testing::IsSupersetOf(std::vector{std::pair{"android.bootanim", "0"}}));
}

TEST(BootProperties, Logcat) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    AndroidOptions opts{.logcat = "*:S Zygote:E"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    EXPECT_THAT(
            getBootProperties(emu.config()),
            testing::IsSupersetOf(std::vector{std::pair{"androidboot.logcat", "*:S,Zygote:E"}}));
}

TEST(Initrd, Basic) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    android::base::file::mkdir_recursive(launcher_path / "content", 0755).IgnoreError();
    android::base::file::mkdir_recursive(launcher_path / "system", 0755).IgnoreError();

    auto system_initrd = launcher_path / "system/ramdisk.img";
    std::ofstream{system_initrd};

    FakeEmulator emu;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), GetSystemImageFilePath(Avd::ImageType::RAMDISK))
            .Times(1)
            .WillRepeatedly(testing::Return(system_initrd.string()));
    EXPECT_CALL(emu.mock_avd(), GetContentPath())
            .Times(1)
            .WillRepeatedly(testing::Return((launcher_path / "content").string()));
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    InitrdDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-initrd"),
                        testing::EndsWith(fs::path("content/initrd").make_preferred().string())));
}

TEST(Initrd, RamdiskFlag) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    android::base::file::mkdir_recursive(launcher_path / "content", 0755).IgnoreError();
    android::base::file::mkdir_recursive(launcher_path / "system", 0755).IgnoreError();

    auto system_initrd = launcher_path / "system/ramdisk.img";
    std::ofstream{system_initrd};

    auto override_initrd = launcher_path / "system/other-initrd";
    std::ofstream{override_initrd} << "abc";

    char override_str[1024];
    strncpy(override_str, override_initrd.string().c_str(), 1024);
    AndroidOptions opts{.ramdisk = override_str};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), GetSystemImageFilePath(Avd::ImageType::RAMDISK))
            .Times(0)
            .WillRepeatedly(testing::Return(system_initrd.string()));
    EXPECT_CALL(emu.mock_avd(), GetContentPath())
            .Times(1)
            .WillRepeatedly(testing::Return((launcher_path / "content").string()));
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    InitrdDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    auto params = dev.getQemuParameters(emu.config());

    EXPECT_THAT(params,
                testing::ElementsAre(
                        testing::Eq("-initrd"),
                        testing::EndsWith(fs::path("content/initrd").make_preferred().string())));
    std::string contents;
    std::ifstream{params[1]} >> contents;
    EXPECT_THAT(contents, testing::StartsWith("abc"));
}

}  // namespace android::goldfish::test
