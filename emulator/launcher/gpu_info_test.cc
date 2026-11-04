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

#include "emulator/launcher/gpu_info.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "android/base/system.h"
#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "android/status/status_matcher_macros.h"
#include "emulator/launcher/testing/fake_vulkan_loader.h"

namespace goldfish::gpu::test {

class GpuInfoTest : public ::testing::Test {
  protected:
    void SetUp() override { SetVulkanLoaderForTesting(nullptr); }
    void TearDown() override { SetVulkanLoaderForTesting(nullptr); }

    ::android::base::TestSystem sys_{""};
};

TEST_F(GpuInfoTest, BoundedCStrViewHandlesUntruncatedFixedBuffer) {
    VulkanGpuDeviceInfo info{};
    std::memset(info.properties.deviceName, 'X', sizeof(info.properties.deviceName));
    EXPECT_EQ(info.DeviceName().size(), sizeof(info.properties.deviceName));
}

TEST_F(GpuInfoTest, ProbesPhysicalDevicesWithNonAsciiDirectoryPath) {
    ::android::base::TestTempDir temp_dir("gpu_info_test");
    const std::filesystem::path non_ascii_lib64 = temp_dir.Path() / "测试_🚀_é" / "lib64";
    ASSERT_OK_AND_ASSIGN(const std::filesystem::path vulkan_libdir,
                         CreateFakeVulkanLibDir(non_ascii_lib64));

    auto loader = std::make_shared<FakeVulkanLoader>(std::vector<FakePhysicalDevice>{
        MakeFakePhysicalDevice("Intel(R) UHD Graphics", 0x8086, 0x3e92,
                               VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
                               VK_MAKE_API_VERSION(0, 1, 2, 0), 100, 4294967296ULL, true),
        MakeFakePhysicalDevice("Compute Only Accelerator", 0x10de, 0x1234,
                               VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                               VK_MAKE_API_VERSION(0, 1, 3, 0), 535, 8589934592ULL, false),
    });
    SetVulkanLoaderForTesting(loader);

    const std::vector<VulkanGpuDeviceInfo> devices =
            ProbeVulkanGpuDevices(vulkan_libdir, GetHostVulkanIcd());
    EXPECT_TRUE(loader->instance_destroyed());

    ASSERT_EQ(devices.size(), 2U);
    EXPECT_EQ(devices[0].DeviceName(), "Intel(R) UHD Graphics");
    EXPECT_EQ(devices[0].device_local_memory_bytes, 4294967296ULL);
    EXPECT_TRUE(devices[0].has_graphics_queue_family);

    EXPECT_EQ(devices[1].DeviceName(), "Compute Only Accelerator");
    EXPECT_EQ(devices[1].device_local_memory_bytes, 8589934592ULL);
    EXPECT_FALSE(devices[1].has_graphics_queue_family);
}

TEST_F(GpuInfoTest, RestoresVulkanEnvVarsAfterProbing) {
    ::android::base::TestTempDir temp_dir("gpu_info_env_guard");
    ASSERT_OK_AND_ASSIGN(const std::filesystem::path vulkan_libdir,
                         CreateFakeVulkanLibDir(temp_dir.Path()));

    auto loader = std::make_shared<FakeVulkanLoader>(std::vector<FakePhysicalDevice>{
        MakeFakePhysicalDevice("Apple M3 Max", 0x106b, 0x0001,
                               VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
                               VK_MAKE_API_VERSION(0, 1, 3, 0), 1, 34359738368ULL, true),
    });
    SetVulkanLoaderForTesting(loader);

    ::android::base::System::SetEnvironmentVariable("VK_DRIVER_FILES",
                                                    "/preexisting/driver_icd.json");
    ::android::base::System::SetEnvironmentVariable("VK_ICD_FILENAMES",
                                                    "/preexisting/icd_filenames.json");
    ::android::base::System::SetEnvironmentVariable("VK_ADD_DRIVER_FILES",
                                                    "/preexisting/add_driver.json");
    ::android::base::System::SetEnvironmentVariable("VK_LOADER_DRIVERS_DISABLE", "*preexisting*");

    const auto probed = ProbeVulkanGpuDevices(vulkan_libdir, GetHostVulkanIcd());
    ASSERT_EQ(probed.size(), 1U);

#if defined(__APPLE__)
    EXPECT_NE(loader->observed_vk_driver_files(), "/preexisting/driver_icd.json");
    EXPECT_FALSE(loader->observed_vk_driver_files().empty());
    EXPECT_EQ(loader->observed_vk_loader_drivers_disable(), "");
#else
    EXPECT_EQ(loader->observed_vk_driver_files(), "");
    EXPECT_EQ(loader->observed_vk_loader_drivers_disable(), "*lvp*,*swiftshader*");
#endif
    EXPECT_EQ(loader->observed_vk_icd_filenames(), "");
    EXPECT_EQ(loader->observed_vk_add_driver_files(), "");

    EXPECT_EQ(::android::base::System::GetEnvironmentVariable("VK_DRIVER_FILES"),
              "/preexisting/driver_icd.json");
    EXPECT_EQ(::android::base::System::GetEnvironmentVariable("VK_ICD_FILENAMES"),
              "/preexisting/icd_filenames.json");
    EXPECT_EQ(::android::base::System::GetEnvironmentVariable("VK_ADD_DRIVER_FILES"),
              "/preexisting/add_driver.json");
    EXPECT_EQ(::android::base::System::GetEnvironmentVariable("VK_LOADER_DRIVERS_DISABLE"),
              "*preexisting*");

    ::android::base::System::SetEnvironmentVariable("VK_DRIVER_FILES", "");
    ::android::base::System::SetEnvironmentVariable("VK_ICD_FILENAMES", "");
    ::android::base::System::SetEnvironmentVariable("VK_ADD_DRIVER_FILES", "");
    ::android::base::System::SetEnvironmentVariable("VK_LOADER_DRIVERS_DISABLE", "");
}

TEST_F(GpuInfoTest, ProbesLavapipeSoftwareIcd) {
    ::android::base::TestTempDir temp_dir("gpu_info_lavapipe");
    ASSERT_OK_AND_ASSIGN(const std::filesystem::path vulkan_libdir,
                         CreateFakeVulkanLibDir(temp_dir.Path()));

    auto loader = std::make_shared<FakeVulkanLoader>(
            std::vector<FakePhysicalDevice>{
                MakeFakePhysicalDevice("NVIDIA GeForce RTX 4080", 0x10de, 0x2704,
                                       VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                                       VK_MAKE_API_VERSION(0, 1, 3, 250), 535, 17179869184ULL,
                                       true),
            },
            std::vector<FakePhysicalDevice>{
                MakeFakePhysicalDevice("llvmpipe (LLVM 21.1.8, 256 bits)", 0x10005, 0x0,
                                       VK_PHYSICAL_DEVICE_TYPE_CPU,
                                       VK_MAKE_API_VERSION(0, 1, 4, 305), 1, 0ULL, true),
            });
    SetVulkanLoaderForTesting(loader);

    const std::vector<VulkanGpuDeviceInfo> software_devices =
            ProbeVulkanGpuDevices(vulkan_libdir, "lavapipe");
    EXPECT_TRUE(loader->instance_destroyed());
    EXPECT_NE(loader->observed_vk_driver_files().find("lvp_icd."), std::string::npos);
    EXPECT_EQ(loader->observed_vk_loader_drivers_disable(), "");

    ASSERT_EQ(software_devices.size(), 1U);
    EXPECT_EQ(software_devices[0].DeviceName(), "llvmpipe (LLVM 21.1.8, 256 bits)");
    EXPECT_EQ(software_devices[0].properties.deviceType, VK_PHYSICAL_DEVICE_TYPE_CPU);
    EXPECT_EQ(software_devices[0].properties.vendorID, 0x10005U);
}

}  // namespace goldfish::gpu::test
