// Copyright 2024 The Android Open Source Project
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
#pragma once
#include <filesystem>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/hardware_config.h"

namespace android::goldfish {
namespace fs = std::filesystem;

using android::base::operator""_GiB;

/**
 * @brief A fake implementation of the Avd class for testing purposes.
 *
 * This class provides a mock implementation of the Avd interface, allowing
 * developers to simulate an Android Virtual Device (AVD) without needing a
 * real AVD setup. It's particularly useful for unit testing components that
 * interact with AVDs.
 *
 * The FakeAvd class allows you to:
 *   - Set and get various AVD properties (name, content path, API level, etc.).
 *   - Simulate different device types and CPU architectures.
 *   - Control whether the AVD has Play Store or encryption key support.
 *   - Override hardware configuration settings.
 *   - Set custom image file paths for testing.
 *
 * @note This class is intended for testing and should not be used in production code.
 *
 * @par Example of overriding hardware config:
 * ```cpp
 *   FakeAvd avd;
 *   // Override the number of CPU cores
 *   avd.mutable_hw().hw_cpu_ncore = 8;
 *   // Override the RAM size
 *   avd.mutable_hw().hw_ramSize = 4096;
 *   // Override the device name
 *   avd.mutable_hw().hw_device_name = "pixel_fold";
 * ```
 */
class FakeAvd : public Avd {
  public:
    FakeAvd()
        : mName("default_fake_avd"),
          mContentPath("/tmp/fake_avd"),
          mApiLevel(35),
          mDessert("V"),
          mApiDescription("15.0 (V) - API 35"),
          mPlaystore(true) {
        // Initialize mHwCfg with values from the provided .ini file
        mHwCfg.hw_cpu_arch = "arm64";
        mHwCfg.hw_cpu_ncore = 4;
        mHwCfg.hw_ramSize = 2048;
        mHwCfg.hw_screen = "multi-touch";
        mHwCfg.hw_mainKeys = false;
        mHwCfg.hw_trackBall = false;
        mHwCfg.hw_keyboard = true;
        mHwCfg.hw_keyboard_lid = false;
        mHwCfg.hw_keyboard_charmap = "qwerty2";
        mHwCfg.hw_dPad = false;
        mHwCfg.hw_rotaryInput = false;
        mHwCfg.hw_gsmModem = true;
        mHwCfg.hw_gps = true;
        mHwCfg.hw_battery = true;
        mHwCfg.hw_accelerometer = true;
        mHwCfg.hw_accelerometer_uncalibrated = true;
        mHwCfg.hw_gyroscope = true;
        mHwCfg.hw_audioInput = true;
        mHwCfg.hw_audioOutput = true;
        mHwCfg.hw_sdCard = true;
        mHwCfg.disk_cachePartition = true;
        mHwCfg.disk_cachePartition_path = "~/../avd/35.avd/cache.img";
        mHwCfg.disk_cachePartition_size = 66_MiB;
        mHwCfg.test_quitAfterBootTimeOut = -1;
        mHwCfg.test_delayAdbTillBootComplete = 0;
        mHwCfg.test_monitorAdb = 0;
        mHwCfg.hw_lcd_width = 1080;
        mHwCfg.hw_lcd_height = 2424;
        mHwCfg.hw_lcd_depth = 16;
        mHwCfg.hw_lcd_circular = false;
        mHwCfg.hw_lcd_density = 420;
        mHwCfg.hw_lcd_backlight = true;
        mHwCfg.hw_lcd_vsync = 60;
        mHwCfg.hw_gltransport = "pipe";
        mHwCfg.hw_gltransport_asg_writeBufferSize = 1048576;
        mHwCfg.hw_gltransport_asg_writeStepSize = 4096;
        mHwCfg.hw_gltransport_asg_dataRingSize = 32768;
        mHwCfg.hw_gltransport_drawFlushInterval = 800;
        mHwCfg.hw_displayRegion_0_1_xOffset = -1;
        mHwCfg.hw_displayRegion_0_1_yOffset = -1;
        mHwCfg.hw_displayRegion_0_1_width = 0;
        mHwCfg.hw_displayRegion_0_1_height = 0;
        mHwCfg.hw_displayRegion_0_2_xOffset = -1;
        mHwCfg.hw_displayRegion_0_2_yOffset = -1;
        mHwCfg.hw_displayRegion_0_2_width = 0;
        mHwCfg.hw_displayRegion_0_2_height = 0;
        mHwCfg.hw_displayRegion_0_3_xOffset = -1;
        mHwCfg.hw_displayRegion_0_3_yOffset = -1;
        mHwCfg.hw_displayRegion_0_3_width = 0;
        mHwCfg.hw_displayRegion_0_3_height = 0;
        mHwCfg.hw_display1_width = 0;
        mHwCfg.hw_display1_height = 0;
        mHwCfg.hw_display1_density = 0;
        mHwCfg.hw_display1_xOffset = -1;
        mHwCfg.hw_display1_yOffset = -1;
        mHwCfg.hw_display1_flag = 0;
        mHwCfg.hw_display2_width = 0;
        mHwCfg.hw_display2_height = 0;
        mHwCfg.hw_display2_density = 0;
        mHwCfg.hw_display2_xOffset = -1;
        mHwCfg.hw_display2_yOffset = -1;
        mHwCfg.hw_display2_flag = 0;
        mHwCfg.hw_display3_width = 0;
        mHwCfg.hw_display3_height = 0;
        mHwCfg.hw_display3_density = 0;
        mHwCfg.hw_display3_xOffset = -1;
        mHwCfg.hw_display3_yOffset = -1;
        mHwCfg.hw_display3_flag = 0;
        mHwCfg.hw_multi_display_window = false;
        mHwCfg.hw_hotplug_multi_display = false;
        mHwCfg.hw_gpu_enabled = true;
        mHwCfg.hw_gpu_mode = "host";
        mHwCfg.hw_initialOrientation = "portrait";
        mHwCfg.hw_camera_back = "virtualscene";
        mHwCfg.hw_camera_front = "emulated";
        mHwCfg.vm_heapSize = 512;
        mHwCfg.hw_sensors_light = true;
        mHwCfg.hw_sensors_pressure = true;
        mHwCfg.hw_sensors_humidity = true;
        mHwCfg.hw_sensors_proximity = true;
        mHwCfg.hw_sensors_magnetic_field = true;
        mHwCfg.hw_sensors_magnetic_field_uncalibrated = true;
        mHwCfg.hw_sensors_gyroscope_uncalibrated = true;
        mHwCfg.hw_sensors_orientation = true;
        mHwCfg.hw_sensors_temperature = true;
        mHwCfg.hw_sensors_rgbclight = false;
        mHwCfg.hw_sensor_hinge = false;
        mHwCfg.hw_sensor_hinge_count = 0;
        mHwCfg.hw_sensor_hinge_type = 0;
        mHwCfg.hw_sensor_hinge_sub_type = 0;
        mHwCfg.hw_sensor_hinge_resizable_config = 1;
        mHwCfg.hw_sensor_hinge_fold_to_displayRegion_0_1_at_posture = 1;
        mHwCfg.hw_sensor_roll = false;
        mHwCfg.hw_sensor_roll_count = 0;
        mHwCfg.hw_sensor_roll_resize_to_displayRegion_0_1_at_posture = 6;
        mHwCfg.hw_sensor_roll_resize_to_displayRegion_0_2_at_posture = 6;
        mHwCfg.hw_sensor_roll_resize_to_displayRegion_0_3_at_posture = 6;
        mHwCfg.hw_sensors_heart_rate = false;
        mHwCfg.hw_sensors_wrist_tilt = false;
        mHwCfg.hw_useext4 = true;
        mHwCfg.hw_arc = false;
        mHwCfg.hw_arc_autologin = false;
        mHwCfg.hw_device_name = "pixel_9";
        mHwCfg.kernel_path =
                "~/Android/sdk/system-images/android-35/google_apis_playstore/arm64-v8a//"
                "kernel-ranchu";
        mHwCfg.kernel_newDeviceNaming = "yes";
        mHwCfg.kernel_supportsYaffs2 = "no";
        mHwCfg.disk_ramdisk_path =
                "~/Android/sdk/system-images/android-35/google_apis_playstore/arm64-v8a//"
                "ramdisk.img";
        mHwCfg.disk_systemPartition_initPath =
                "~/Android/sdk/system-images/android-35/google_apis_playstore/arm64-v8a//"
                "system.img";
        mHwCfg.disk_systemPartition_size = 2101_MiB;
        mHwCfg.disk_vendorPartition_initPath =
                "~/Android/sdk/system-images/android-35/google_apis_playstore/arm64-v8a//"
                "vendor.img";
        mHwCfg.disk_vendorPartition_size = 800_MiB;
        mHwCfg.disk_dataPartition_path = "~/../avd/35.avd/userdata-qemu.img";
        mHwCfg.disk_dataPartition_size = 6_GiB;
        mHwCfg.disk_encryptionKeyPartition_path = "~/../avd/35.avd/encryptionkey.img";
        mHwCfg.PlayStore_enabled = true;
        mHwCfg.avd_name = "35";
        mHwCfg.avd_id = "35";
        mHwCfg.fastboot_forceColdBoot = false;
        mHwCfg.userdata_useQcow2 = true;
    }

    std::string details(bool verbose) const override { return "FakeAvd: " + mName; }

    std::string name() const override { return mName; }
    void setName(const std::string& name) { mName = name; }

    DeviceType getDeviceType() const override { return mDeviceType; }
    void setDeviceType(DeviceType type) { mDeviceType = type; }

    fs::path getContentPath() const override { return mContentPath; }
    void setContentPath(const fs::path& path) { mContentPath = path; }

    absl::StatusOr<fs::path> getSystemImageFilePath(Avd::ImageType imgType) const override {
        if (mSystemImageFilePaths.count(imgType)) {
            return mSystemImageFilePaths.at(imgType);
        }
        return absl::NotFoundError("System image file not found");
    }
    void setSystemImageFilePath(Avd::ImageType imgType, const fs::path& path) {
        mSystemImageFilePaths[imgType] = path;
    }

    void setHasEncryptionKey(bool hasKey) { mHasEncryptionKey = hasKey; }

    CpuArchitecture detectArchitecture() const override { return mCpuArchitecture; }
    void setCpuArchitecture(CpuArchitecture arch) { mCpuArchitecture = arch; }

    fs::path getSdkPath() const override {}
    fs::path getAvdPath() const override {}

    const HardwareConfig& hw() const override { return mHwCfg; }
    HardwareConfig& mutable_hw() { return mHwCfg; }

    bool playstore() const override { return mPlaystore; }
    void setPlaystore(bool playstore) { mPlaystore = playstore; }

    int apiLevel() const override { return mApiLevel; }
    void setApiLevel(int level) { mApiLevel = level; }

    std::string dessert() const override { return mDessert; }
    void setDessert(const std::string& dessert) { mDessert = dessert; }

    std::string apiDescription() const override { return mApiDescription; }
    void setApiDescription(const std::string& description) { mApiDescription = description; }

    fs::path getConfigIniPath() const override { return mIniFile; }
    void setIniFile(const fs::path& iniFile) { mIniFile = iniFile; }

    std::string display_name() const override { return mDisplayName; }
    void setDisplayName(const std::string& displayName) { mDisplayName = displayName; }

    std::string id() const override {
        return name();
    }

    std::string abi() const override {
        return "";
    }

    std::string build_sdk() const override {
        return "";
    }

    std::string build_id() const override {
        return "";
    }

    std::string build_flavour() const override {
        return "";
    }

  private:
    std::string mName;
    DeviceType mDeviceType = DeviceType::kPhone;
    fs::path mContentPath;
    std::unordered_map<Avd::ImageType, fs::path> mImageFilePaths;
    std::unordered_map<Avd::ImageType, fs::path> mSystemImageFilePaths;
    bool mHasEncryptionKey = false;
    CpuArchitecture mCpuArchitecture = CpuArchitecture::kArm;
    HardwareConfig mHwCfg;
    bool mPlaystore;
    int mApiLevel;
    std::string mDessert;
    std::string mApiDescription;
    fs::path mIniFile;
    std::string mDisplayName;
};

}  // namespace android::goldfish
