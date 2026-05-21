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

#include "android/goldfish/memory_config.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"

#include "android/goldfish/hardware_config.h"

namespace android::goldfish {

class MemoryConfigTest : public ::testing::Test {
  protected:
    HardwareConfig hw;

    void SetUp() override {
        hw = HardwareConfig();
        // Set some defaults to avoid division by zero or other issues
        hw.hw_lcd_width = 1080;
        hw.hw_lcd_height = 1920;
        hw.hw_lcd_density = 420;
        hw.hw_ramSize = 2048;
        hw.vm_heapSize = 128;
    }
};

TEST_F(MemoryConfigTest, Basic) {
    hw.hw_ramSize = 512;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 21);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(hw.hw_ramSize, 1024);
}

TEST_F(MemoryConfigTest, Default) {
    hw.hw_ramSize = 0;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 30);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(hw.hw_ramSize, 2048);
}

TEST_F(MemoryConfigTest, Api34Minimum) {
    hw.hw_ramSize = 512;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 34);
    EXPECT_TRUE(status.ok());
    // Minimum for API 34 is 2560
    EXPECT_EQ(hw.hw_ramSize, 2560);
}

TEST_F(MemoryConfigTest, Api34FoldableMinimum) {
    hw.hw_ramSize = 512;
    hw.hw_sensor_hinge = true;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 34);
    EXPECT_TRUE(status.ok());
    // Minimum for API 34 Foldable is 4096
    EXPECT_EQ(hw.hw_ramSize, 4096);
}

TEST_F(MemoryConfigTest, Api34LargeScreenMinimum) {
    hw.hw_ramSize = 512;
    hw.hw_lcd_width = 2000;
    hw.hw_lcd_height = 2000;  // 4M pixels > 3M
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 34);
    EXPECT_TRUE(status.ok());
    // Minimum for API 34 Large Screen is 4096
    EXPECT_EQ(hw.hw_ramSize, 4096);
}

TEST_F(MemoryConfigTest, Api37Minimum) {
    hw.hw_ramSize = 512;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 37);
    EXPECT_TRUE(status.ok());
    // Minimum for API 37 is 4096
    EXPECT_EQ(hw.hw_ramSize, 4096);
}

TEST_F(MemoryConfigTest, BasicHeapSize) {
    hw.hw_ramSize = 2048;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 30);
    EXPECT_TRUE(status.ok());

    // Exact values might depend on internal tables, but we expect it to increase.
    EXPECT_GE(hw.vm_heapSize, 128);
    EXPECT_LE(hw.vm_heapSize, 576);
}

TEST_F(MemoryConfigTest, SmallRamForcesSmallHeap) {
    hw.hw_ramSize = 512;
    hw.vm_heapSize = 256;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 21);
    EXPECT_TRUE(status.ok());
    EXPECT_GE(hw.vm_heapSize, 32);
}

TEST_F(MemoryConfigTest, HeapSizeIncreasesRam) {
    // If we have a large heap requirement, it should push up RAM.
    hw.hw_ramSize = 64;
    hw.hw_lcd_density = 640;  // XXXHDPI
    hw.hw_lcd_width = 2000;
    hw.hw_lcd_height = 3000;  // Large screen

    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 30);
    EXPECT_TRUE(status.ok());
    EXPECT_GE(hw.hw_ramSize, hw.vm_heapSize * 2);
}

TEST_F(MemoryConfigTest, CappedHeapSize) {
    hw.hw_ramSize = 8192;  // 8GB RAM
    hw.vm_heapSize = 1024;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 30);
    EXPECT_TRUE(status.ok());
    // Should be capped at 576
    EXPECT_EQ(hw.vm_heapSize, 576);
}

TEST_F(MemoryConfigTest, LowDensitySmallHeap) {
    hw.hw_ramSize = 1024;
    hw.hw_lcd_density = 120;  // LDPI
    hw.hw_lcd_width = 320;
    hw.hw_lcd_height = 480;
    hw.vm_heapSize = 256;
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 23);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(hw.vm_heapSize, 256);
}

TEST_F(MemoryConfigTest, HighDensityLargeHeap) {
    hw.hw_ramSize = 4096;
    hw.hw_lcd_density = 640;  // XXXHDPI
    hw.hw_lcd_width = 1440;
    hw.hw_lcd_height = 2560;
    hw.vm_heapSize = 32;  // Too small
    auto status = MemoryConfig::FinalizeRamAndHeapSize(hw, 30);
    EXPECT_TRUE(status.ok());
    EXPECT_GE(hw.vm_heapSize, 256);
}

TEST_F(MemoryConfigTest, CalculateMinimumRam_ApiLevels) {
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 21), 1024);
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 26), 1536);
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 29), 2048);
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 34), 2560);
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 37), 4096);
}

TEST_F(MemoryConfigTest, CalculateMinimumRam_DeviceType) {
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 30, DeviceType::kTv),
              2048);  // API 30 requires 2048, which is > 1024
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 21, DeviceType::kTv),
              1024);  // API 21 requires 1024
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 21, DeviceType::kXr),
              4096);  // XR requires 4096
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 37, DeviceType::kWear),
              2048);  // Wear capped at 2048 even for API 37
    EXPECT_EQ(MemoryConfig::CalculateMinimumRam(hw, 26, DeviceType::kWear),
              1536);  // Wear retains lower min_ram for older APIs
}

TEST_F(MemoryConfigTest, CalculateMinimumRam_DeathOnLowApi) {
    EXPECT_DEATH(MemoryConfig::CalculateMinimumRam(hw, 20), "We do not support api level < 21");
}

}  // namespace android::goldfish
