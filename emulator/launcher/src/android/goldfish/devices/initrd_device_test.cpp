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

#include <android/base/system/System.h>
#include <android/goldfish/config/hardware_config.h>
#include <gtest/gtest.h>

#include <memory>
#include <fstream>

#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestSystem.h"
#include "android/cmdline-definitions.h"
#include "android/goldfish/config/emulator.h"
#include "mock_avd.h"

namespace android::goldfish::test {

TEST(BootProperties, Basic) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    // First the 3 calls by Emulator ctor.
    EXPECT_CALL(*avd_ptr, name()).Times(2).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(*avd_ptr, hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(*avd_ptr, getIniFile()).WillOnce(testing::Return("some/path/mock_avd.ini"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    EXPECT_THAT(getBootProperties(emu), testing::IsSupersetOf(std::vector{
        std::pair{"androidboot.hardware", "ranchu"},
        std::pair{"androidboot.logcat", "*:V"},
    }));
}

TEST(BootProperties, NoBootAnim) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    // First the 3 calls by Emulator ctor.
    EXPECT_CALL(*avd_ptr, name()).Times(2).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(*avd_ptr, hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(*avd_ptr, getIniFile()).WillOnce(testing::Return("some/path/mock_avd.ini"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    AndroidOptions opts{.no_boot_anim = true};
    Emulator emu(std::move(avd), std::move(opts));

    EXPECT_THAT(getBootProperties(emu), testing::IsSupersetOf(std::vector{std::pair{"android.bootanim", "0"}}));
}

TEST(BootProperties, Logcat) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    // First the 3 calls by Emulator ctor.
    EXPECT_CALL(*avd_ptr, name()).Times(2).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(*avd_ptr, hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(*avd_ptr, getIniFile()).WillOnce(testing::Return("some/path/mock_avd.ini"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    AndroidOptions opts{.logcat = "*:S Zygote:E"};
    Emulator emu(std::move(avd), std::move(opts));

    EXPECT_THAT(getBootProperties(emu), testing::IsSupersetOf(std::vector{std::pair{"androidboot.logcat", "*:S,Zygote:E"}}));
}

TEST(Initrd, Basic) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);

    std::filesystem::create_directories(launcher_path / "content");
    std::filesystem::create_directories(launcher_path / "system");

    auto system_initrd = launcher_path / "system/system-initrd";
    std::ofstream{system_initrd};

    auto hw = HardwareConfig();
    hw.disk_ramdisk_path = system_initrd.string();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    // First the 3 calls by Emulator ctor.
    EXPECT_CALL(*avd_ptr, name()).Times(2).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(*avd_ptr, hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(*avd_ptr, getIniFile()).WillOnce(testing::Return("some/path/mock_avd.ini"));

    EXPECT_CALL(*avd_ptr, getContentPath())
            .Times(2).WillRepeatedly(testing::Return((launcher_path/ "content").string()));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    InitrdDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(
                                     testing::Eq("-initrd"), testing::EndsWith("content/initrd")));
}

}  // namespace android::goldfish::test
