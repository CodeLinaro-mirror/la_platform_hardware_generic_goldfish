// Copyright (C) 2026 The Android Open Source Project
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

#include "capabilities_service_impl.h"

#include <cstdint>
#include <string>

#include "common/common.pb.h"
#include "discovery/capabilities_service.pb.h"

#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

namespace goldfish::grpc::v2 {

using ::android::emulation::v2::common::AccessMode;
using ::android::emulation::v2::common::ChargerSource;
using ::android::emulation::v2::common::ConcurrencyProfile;
using ::android::emulation::v2::common::FoldAxis;
using ::android::emulation::v2::common::HingeConstruction;
using ::android::emulation::v2::common::ImageFormat;
using ::android::emulation::v2::common::PostureType;
using ::android::emulation::v2::common::RollDirection;
using ::android::emulation::v2::common::SensorType;
using ::android::emulation::v2::discovery::BatteryCapabilities;
using ::android::emulation::v2::discovery::BiometricsCapabilities;
using ::android::emulation::v2::discovery::ClipboardCapabilities;
using ::android::emulation::v2::discovery::DeviceCapabilities;
using ::android::emulation::v2::discovery::DeviceMetadata;
using ::android::emulation::v2::discovery::DisplayCapabilities;
using ::android::emulation::v2::discovery::GetDeviceCapabilitiesRequest;
using ::android::emulation::v2::discovery::InputCapabilities;
using ::android::emulation::v2::discovery::LocationCapabilities;
using ::android::emulation::v2::discovery::PostureCapabilities;
using ::android::emulation::v2::discovery::ScreenCaptureCapabilities;
using ::android::emulation::v2::discovery::SensorsCapabilities;
using ::android::emulation::v2::discovery::SnapshotCapabilities;
using ::android::emulation::v2::discovery::SystemCapabilities;
using ::android::emulation::v2::discovery::VmCapabilities;
using ::android::goldfish::HardwareConfig;
using ::goldfish::avd_info::AvdProperties;
using ::grpc::ServerContext;
using ::grpc::Status;

namespace {

void PopulateMetadata(const AvdProperties& props, const HardwareConfig& hw, DeviceMetadata* meta) {
    meta->set_display_name(props.avd_name.empty() ? "Android Emulator" : props.avd_name);
    meta->set_model_id(props.avd_id.empty() ? "emulator" : props.avd_id);
    meta->set_api_level(props.avd_api);
    meta->set_concurrency_profile(ConcurrencyProfile::CONCURRENCY_PROFILE_SINGLE_TENANT_PERSISTENT);
    meta->set_cpu_architecture(props.avd_abi.empty() ? "x86_64" : props.avd_abi);
    meta->set_cpu_core_count(hw.hw_cpu_ncore > 0 ? hw.hw_cpu_ncore : 1);
    meta->set_total_ram_bytes(static_cast<int64_t>(hw.hw_ramSize > 0 ? hw.hw_ramSize : 2048) *
                              1024 * 1024);
    meta->set_supports_play_store(hw.PlayStore_enabled);
    meta->set_manufacturer("Google");
    meta->set_brand("Google");
    meta->set_total_storage_bytes(hw.disk_dataPartition_size.Bytes() > 0
                                          ? static_cast<int64_t>(hw.disk_dataPartition_size.Bytes())
                                          : static_cast<int64_t>(6ULL * 1024 * 1024 * 1024));
}

void PopulateDisplayCapabilities(const HardwareConfig& hw, DisplayCapabilities* display_caps) {
    display_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    display_caps->set_supports_virtual_display_creation(true);
    display_caps->set_max_supported_display_count(4);

    auto* primary = display_caps->add_built_in_displays();
    primary->set_display_id(0);
    primary->set_width(hw.hw_lcd_width > 0 ? hw.hw_lcd_width : 1080);
    primary->set_height(hw.hw_lcd_height > 0 ? hw.hw_lcd_height : 2400);
    primary->set_density_dpi(hw.hw_lcd_density > 0 ? hw.hw_lcd_density : 420);
    primary->set_circular(hw.hw_lcd_circular);
    primary->set_refresh_rate_hz(60.0f);
    primary->set_supports_dynamic_resizing(true);
}

void PopulateBatteryCapabilities(const HardwareConfig& hw, BatteryCapabilities* battery_caps) {
    battery_caps->set_access_mode(hw.hw_battery ? AccessMode::ACCESS_MODE_READ_WRITE
                                                : AccessMode::ACCESS_MODE_UNSPECIFIED);
    battery_caps->add_supported_chargers(ChargerSource::CHARGER_SOURCE_AC);
    battery_caps->add_supported_chargers(ChargerSource::CHARGER_SOURCE_USB);
    battery_caps->add_supported_chargers(ChargerSource::CHARGER_SOURCE_WIRELESS);
}

void PopulateLocationCapabilities(const HardwareConfig& hw, LocationCapabilities* loc_caps) {
    loc_caps->set_access_mode(hw.hw_gps ? AccessMode::ACCESS_MODE_READ_WRITE
                                        : AccessMode::ACCESS_MODE_UNSPECIFIED);
    loc_caps->set_supports_altitude(true);
    loc_caps->set_supports_speed(true);
    loc_caps->set_supports_bearing(true);
    loc_caps->set_supports_accuracy(true);
    loc_caps->set_supports_satellites(true);
}

void PopulateSensorsCapabilities(const HardwareConfig& hw, SensorsCapabilities* sensor_caps) {
    sensor_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    if (hw.hw_accelerometer) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_ACCELEROMETER);
    }
    if (hw.hw_gyroscope) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_GYROSCOPE);
    }
    if (hw.hw_sensors_magnetic_field) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_MAGNETOMETER);
    }
    if (hw.hw_sensors_orientation) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_ORIENTATION);
    }
    if (hw.hw_sensors_temperature) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_AMBIENT_TEMPERATURE);
    }
    if (hw.hw_sensors_proximity) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_PROXIMITY);
    }
    if (hw.hw_sensors_light) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_LIGHT);
    }
    if (hw.hw_sensors_pressure) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_PRESSURE);
    }
    if (hw.hw_sensors_humidity) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_RELATIVE_HUMIDITY);
    }
    if (hw.hw_sensors_heart_rate) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_HEART_RATE);
    }
    if (hw.hw_sensors_rgbclight) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_RGBC_LIGHT);
    }
    if (hw.hw_sensors_wrist_tilt) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_WRIST_TILT);
    }
    if (hw.hw_accelerometer_uncalibrated) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED);
    }
    if (hw.hw_sensors_gyroscope_uncalibrated) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_GYROSCOPE_UNCALIBRATED);
    }
    if (hw.hw_sensors_magnetic_field_uncalibrated) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_MAGNETOMETER_UNCALIBRATED);
    }
    if (hw.hw_sensor_hinge) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_HINGE_ANGLE);
    }
    if (hw.hw_sensor_roll) {
        sensor_caps->add_available_sensors(SensorType::SENSOR_TYPE_ROLLABLE);
    }
}

void PopulateInputCapabilities(const HardwareConfig& hw, InputCapabilities* input_caps) {
    input_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    input_caps->set_supports_touch(true);
    input_caps->set_supports_multi_touch(true);
    input_caps->set_max_touch_point_count(10);
    input_caps->set_supports_keyboard(hw.hw_keyboard);
    // TODO(jansene): These should align with what we tell the system image.
    // i.e. features: VirtioMouse, VirtioDualModeMouse
    input_caps->set_supports_mouse_pointer(false);
    input_caps->set_supports_relative_mouse(false);
    input_caps->set_supports_rotary_encoder(hw.hw_rotaryInput);
}

void PopulateVmCapabilities(VmCapabilities* vm_caps) {
    vm_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    vm_caps->set_supports_pause_resume(true);
    vm_caps->set_supports_hard_reset(true);
    vm_caps->set_supports_shutdown(true);
}

void PopulateSystemCapabilities(SystemCapabilities* sys_caps) {
    sys_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    sys_caps->set_supports_reboot(true);
}

void PopulateScreenCaptureCapabilities(ScreenCaptureCapabilities* media_caps) {
    media_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    media_caps->add_supported_formats(ImageFormat::IMAGE_FORMAT_PNG);
    media_caps->add_supported_formats(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
    media_caps->add_supported_formats(ImageFormat::IMAGE_FORMAT_JPEG);
    media_caps->set_supports_row_stride(true);
}

void PopulateBiometricsCapabilities(BiometricsCapabilities* bio_caps) {
    bio_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    bio_caps->set_supports_fingerprint(true);
}

void PopulateClipboardCapabilities(ClipboardCapabilities* clip_caps) {
    clip_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
}

void PopulateSnapshotCapabilities(SnapshotCapabilities* snap_caps) {
    snap_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    snap_caps->set_supports_local_snapshots(true);
    snap_caps->set_supports_cloud_storage(false);
}

void PopulatePostureCapabilities(const HardwareConfig& hw, PostureCapabilities* posture_caps) {
    posture_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    posture_caps->set_supports_hinge_angle(hw.hw_sensor_hinge);
    posture_caps->set_fold_axis(hw.hw_sensor_hinge_type == 0 ? FoldAxis::FOLD_AXIS_HORIZONTAL
                                                             : FoldAxis::FOLD_AXIS_VERTICAL);
    posture_caps->set_hinge_construction(
            hw.hw_sensor_hinge_sub_type == 0
                    ? HingeConstruction::HINGE_CONSTRUCTION_FOLD_ON_SCREEN
                    : HingeConstruction::HINGE_CONSTRUCTION_DUAL_SCREEN_DIVIDER);

    posture_caps->set_supports_rollable(hw.hw_sensor_roll);
    if (hw.hw_sensor_roll) {
        auto* roll_cfg = posture_caps->mutable_rollable();
        roll_cfg->set_direction(hw.hw_sensor_roll_direction == "1"
                                        ? RollDirection::ROLL_DIRECTION_RIGHT_TO_LEFT
                                        : RollDirection::ROLL_DIRECTION_LEFT_TO_RIGHT);
    }
}

}  // namespace

CapabilitiesServiceImpl::CapabilitiesServiceImpl(
        const ::goldfish::avd_info::AvdUniverse& avd_universe)
        : avd_universe_(avd_universe) {}

Status CapabilitiesServiceImpl::GetDeviceCapabilities(
        ServerContext* /*context*/, const GetDeviceCapabilitiesRequest* /*request*/,
        DeviceCapabilities* reply) {
    const auto& props = avd_universe_.Props();
    const auto& hw = props.hw_config;

    PopulateMetadata(props, hw, reply->mutable_metadata());
    PopulateDisplayCapabilities(hw, reply->mutable_display());
    PopulateBatteryCapabilities(hw, reply->mutable_battery());
    PopulateLocationCapabilities(hw, reply->mutable_location());
    PopulateSensorsCapabilities(hw, reply->mutable_sensors());
    PopulateInputCapabilities(hw, reply->mutable_input());
    PopulateVmCapabilities(reply->mutable_vm());
    PopulateSystemCapabilities(reply->mutable_system());
    PopulateScreenCaptureCapabilities(reply->mutable_screen_capture());
    PopulateBiometricsCapabilities(reply->mutable_biometrics());
    PopulateClipboardCapabilities(reply->mutable_clipboard());
    PopulateSnapshotCapabilities(reply->mutable_snapshot());

    if (hw.hw_sensor_hinge || hw.hw_sensor_roll) {
        PopulatePostureCapabilities(hw, reply->mutable_posture());
    }

    return Status::OK;
}

}  // namespace goldfish::grpc::v2
