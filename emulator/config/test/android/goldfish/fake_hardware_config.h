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

#include "android/goldfish/hardware_config.h"

namespace android::goldfish {

using android::base::operator""_GiB;

/**
 * @brief A fake implementation of the HardwareConfig class for testing purposes.
 */
class FakeHardwareConfig {
  public:
    static HardwareConfig GetHwConfig() {
        // Initialize mHwCfg with values from the provided .ini file
        HardwareConfig hw;
        hw.hw_cpu_arch = "arm64";
        hw.hw_cpu_ncore = 4;
        hw.hw_ramSize = 2048;
        hw.hw_screen = "multi-touch";
        hw.hw_mainKeys = false;
        hw.hw_trackBall = false;
        hw.hw_keyboard = true;
        hw.hw_keyboard_lid = false;
        hw.hw_keyboard_charmap = "qwerty2";
        hw.hw_dPad = false;
        hw.hw_rotaryInput = false;
        hw.hw_gsmModem = true;
        hw.hw_gps = true;
        hw.hw_battery = true;
        hw.hw_accelerometer = true;
        hw.hw_accelerometer_uncalibrated = true;
        hw.hw_gyroscope = true;
        hw.hw_audioInput = true;
        hw.hw_audioOutput = true;
        hw.hw_sdCard = true;
        hw.disk_cachePartition = true;
        hw.disk_cachePartition_path = "~/../avd/35.avd/cache.img";
        hw.disk_cachePartition_size = 66_MiB;
        hw.test_quitAfterBootTimeOut = -1;
        hw.test_delayAdbTillBootComplete = 0;
        hw.test_monitorAdb = 0;
        hw.hw_lcd_width = 1080;
        hw.hw_lcd_height = 2424;
        hw.hw_lcd_depth = 16;
        hw.hw_lcd_circular = false;
        hw.hw_lcd_density = 420;
        hw.hw_lcd_backlight = true;
        hw.hw_lcd_vsync = 60;
        hw.hw_gltransport = "pipe";
        hw.hw_gltransport_asg_writeBufferSize = 1048576;
        hw.hw_gltransport_asg_writeStepSize = 4096;
        hw.hw_gltransport_asg_dataRingSize = 32768;
        hw.hw_gltransport_drawFlushInterval = 800;
        hw.hw_displayRegion_0_1_xOffset = -1;
        hw.hw_displayRegion_0_1_yOffset = -1;
        hw.hw_displayRegion_0_1_width = 0;
        hw.hw_displayRegion_0_1_height = 0;
        hw.hw_displayRegion_0_2_xOffset = -1;
        hw.hw_displayRegion_0_2_yOffset = -1;
        hw.hw_displayRegion_0_2_width = 0;
        hw.hw_displayRegion_0_2_height = 0;
        hw.hw_displayRegion_0_3_xOffset = -1;
        hw.hw_displayRegion_0_3_yOffset = -1;
        hw.hw_displayRegion_0_3_width = 0;
        hw.hw_displayRegion_0_3_height = 0;
        hw.hw_display1_width = 0;
        hw.hw_display1_height = 0;
        hw.hw_display1_density = 0;
        hw.hw_display1_xOffset = -1;
        hw.hw_display1_yOffset = -1;
        hw.hw_display1_flag = 0;
        hw.hw_display2_width = 0;
        hw.hw_display2_height = 0;
        hw.hw_display2_density = 0;
        hw.hw_display2_xOffset = -1;
        hw.hw_display2_yOffset = -1;
        hw.hw_display2_flag = 0;
        hw.hw_display3_width = 0;
        hw.hw_display3_height = 0;
        hw.hw_display3_density = 0;
        hw.hw_display3_xOffset = -1;
        hw.hw_display3_yOffset = -1;
        hw.hw_display3_flag = 0;
        hw.hw_multi_display_window = false;
        hw.hw_hotplug_multi_display = false;
        hw.hw_gpu_enabled = true;
        hw.hw_gpu_mode = "host";
        hw.hw_initialOrientation = "portrait";
        hw.hw_camera_back = "virtualscene";
        hw.hw_camera_front = "emulated";
        hw.vm_heapSize = 512;
        hw.hw_sensors_light = true;
        hw.hw_sensors_pressure = true;
        hw.hw_sensors_humidity = true;
        hw.hw_sensors_proximity = true;
        hw.hw_sensors_magnetic_field = true;
        hw.hw_sensors_magnetic_field_uncalibrated = true;
        hw.hw_sensors_gyroscope_uncalibrated = true;
        hw.hw_sensors_orientation = true;
        hw.hw_sensors_temperature = true;
        hw.hw_sensors_rgbclight = false;
        hw.hw_sensor_hinge = false;
        hw.hw_sensor_hinge_count = 0;
        hw.hw_sensor_hinge_type = 0;
        hw.hw_sensor_hinge_sub_type = 0;
        hw.hw_sensor_hinge_resizable_config = 1;
        hw.hw_sensor_hinge_fold_to_displayRegion_0_1_at_posture = 1;
        hw.hw_sensor_roll = false;
        hw.hw_sensor_roll_count = 0;
        hw.hw_sensor_roll_resize_to_displayRegion_0_1_at_posture = 6;
        hw.hw_sensor_roll_resize_to_displayRegion_0_2_at_posture = 6;
        hw.hw_sensor_roll_resize_to_displayRegion_0_3_at_posture = 6;
        hw.hw_sensors_heart_rate = false;
        hw.hw_sensors_wrist_tilt = false;
        hw.hw_useext4 = true;
        hw.hw_arc = false;
        hw.hw_arc_autologin = false;
        hw.hw_device_name = "pixel_9";
        hw.kernel_path =
                "~/Android/sdk/system-images/android-35/google_apis_playstore/arm64-v8a//"
                "kernel-ranchu";
        hw.kernel_newDeviceNaming = "yes";
        hw.kernel_supportsYaffs2 = "no";
        hw.disk_ramdisk_path =
                "~/Android/sdk/system-images/android-35/google_apis_playstore/arm64-v8a//"
                "ramdisk.img";
        hw.disk_systemPartition_initPath =
                "~/Android/sdk/system-images/android-35/google_apis_playstore/arm64-v8a//"
                "system.img";
        hw.disk_systemPartition_size = 2101_MiB;
        hw.disk_vendorPartition_initPath =
                "~/Android/sdk/system-images/android-35/google_apis_playstore/arm64-v8a//"
                "vendor.img";
        hw.disk_vendorPartition_size = 800_MiB;
        hw.disk_dataPartition_path = "~/../avd/35.avd/userdata-qemu.img";
        hw.disk_dataPartition_size = 6_GiB;
        hw.disk_encryptionKeyPartition_path = "~/../avd/35.avd/encryptionkey.img";
        hw.PlayStore_enabled = true;
        hw.avd_name = "35";
        hw.avd_id = "35";
        hw.fastboot_forceColdBoot = false;
        hw.userdata_useQcow2 = true;

        return hw;
    }
};

}  // namespace android::goldfish
