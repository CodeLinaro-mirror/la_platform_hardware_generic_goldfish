// Copyright 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/goldfish/hardware_config.h"

#include <gtest/gtest.h>

#include "android/goldfish/ini_file.h"
#include "goldfish/file/storage_capacity.h"

namespace android::goldfish {
namespace {

using base::operator""_B;
using base::operator""_MiB;

TEST(HardwareConfigTest, DefaultConstructorPopulatesHardwarePropertiesDefaults) {
    HardwareConfig hw;

    // String properties
    EXPECT_EQ(hw.hw_cpu_arch, "arm");
    EXPECT_EQ(hw.hw_cpu_model, "");
    EXPECT_EQ(hw.hw_screen, "multi-touch");
    EXPECT_EQ(hw.hw_gpu_mode, "auto");
    EXPECT_EQ(hw.hw_initialOrientation, "portrait");
    EXPECT_EQ(hw.disk_dataPartition_path, "<temp>");

    // Integer properties
    EXPECT_EQ(hw.hw_cpu_ncore, 2);
    EXPECT_EQ(hw.hw_ramSize, 0);
    EXPECT_EQ(hw.hw_lcd_width, 320);
    EXPECT_EQ(hw.hw_lcd_height, 640);
    EXPECT_EQ(hw.hw_lcd_depth, 16);
    EXPECT_EQ(hw.hw_lcd_density, 160);
    EXPECT_EQ(hw.hw_lcd_vsync, 60);
    EXPECT_EQ(hw.test_quitAfterBootTimeOut, -1);

    // Boolean properties ('yes', 'no', 'false' in hardware-properties.ini)
    EXPECT_TRUE(hw.hw_mainKeys);
    EXPECT_TRUE(hw.hw_gsmModem);
    EXPECT_TRUE(hw.hw_gps);
    EXPECT_TRUE(hw.hw_battery);
    EXPECT_FALSE(hw.hw_keyboard);
    EXPECT_FALSE(hw.hw_sdCard);
    EXPECT_FALSE(hw.hw_gpu_enabled);
    EXPECT_FALSE(hw.fastboot_forceColdBoot);

    // DiskSize properties
    EXPECT_EQ(hw.disk_systemPartition_size, 0_B);
    EXPECT_EQ(hw.disk_vendorPartition_size, 0_B);
    EXPECT_EQ(hw.disk_dataPartition_size, 0_B);
    EXPECT_EQ(hw.sdcard_size, 512_MiB);
}

TEST(HardwareConfigTest, LoadEmptyIniRetainsDefaults) {
    IniFile empty_ini;
    HardwareConfig hw;
    hw.Load(empty_ini);

    EXPECT_EQ(hw.hw_cpu_arch, "arm");
    EXPECT_EQ(hw.hw_cpu_ncore, 2);
    EXPECT_TRUE(hw.hw_mainKeys);
    EXPECT_FALSE(hw.hw_keyboard);
    EXPECT_EQ(hw.disk_dataPartition_size, 0_B);
}

}  // namespace
}  // namespace android::goldfish
