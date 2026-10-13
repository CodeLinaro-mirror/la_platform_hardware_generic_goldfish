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

#include "capabilities_service.pb.h"
#include "common.pb.h"

#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

namespace goldfish::grpc::v2 {

using ::android::emulation::v2::AccessMode;
using ::android::emulation::v2::BatteryCapabilities;
using ::android::emulation::v2::BiometricsCapabilities;
using ::android::emulation::v2::ChargerSource;
using ::android::emulation::v2::ClipboardCapabilities;
using ::android::emulation::v2::DeviceCapabilities;
using ::android::emulation::v2::DeviceKind;
using ::android::emulation::v2::DeviceMetadata;
using ::android::emulation::v2::DisplayCapabilities;
using ::android::emulation::v2::Fidelity;
using ::android::emulation::v2::FoldAxis;
using ::android::emulation::v2::GetDeviceCapabilitiesRequest;
using ::android::emulation::v2::ImageFormat;
using ::android::emulation::v2::InputCapabilities;
using ::android::emulation::v2::InputFeature;
using ::android::emulation::v2::LocationCapabilities;
using ::android::emulation::v2::PostureCapabilities;
using ::android::emulation::v2::ScreenCaptureCapabilities;
using ::android::emulation::v2::SensorsCapabilities;
using ::android::emulation::v2::SensorType;
using ::android::emulation::v2::SnapshotCapabilities;
using ::android::emulation::v2::SnapshotFeature;
using ::android::emulation::v2::SystemCapabilities;
using ::android::emulation::v2::VmCapabilities;
using ::android::emulation::v2::VmOperation;
using ::android::goldfish::HardwareConfig;
using ::goldfish::avd::AvdProperties;
using ::grpc::ServerContext;
using ::grpc::Status;

namespace {

void PopulateMetadata(const AvdProperties& props, const HardwareConfig& hw, DeviceMetadata* meta) {
    // This service only ever runs inside the emulator; a physical device is
    // served by a different implementation reached over ADB.
    meta->set_device_kind(DeviceKind::DEVICE_KIND_EMULATED);
    meta->set_display_name(props.avd_name.empty() ? "Android Emulator" : props.avd_name);
    meta->set_model_id(props.avd_id.empty() ? "emulator" : props.avd_id);
    meta->set_api_level(props.avd_api);
    meta->set_server_version("2.0.0");
    meta->set_server_implementation("goldfish-grpc-v2");
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

void PopulateScreenCaptureCapabilities(ScreenCaptureCapabilities* media_caps) {
    media_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    media_caps->add_supported_formats(ImageFormat::IMAGE_FORMAT_PNG);
    media_caps->add_supported_formats(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
    media_caps->add_supported_formats(ImageFormat::IMAGE_FORMAT_JPEG);
    media_caps->set_supports_row_stride(true);
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

    PopulateScreenCaptureCapabilities(display_caps->mutable_screen_capture());
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
    // The emulated GNSS stack is a real HAL, not an Android framework override.
    loc_caps->set_fidelity(Fidelity::FIDELITY_HARDWARE);
}

void PopulatePostureCapabilities(const HardwareConfig& hw, PostureCapabilities* posture_caps) {
    posture_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    if (hw.hw_sensor_hinge) {
        const FoldAxis axis = hw.hw_sensor_hinge_type == 0 ? FoldAxis::FOLD_AXIS_HORIZONTAL
                                                           : FoldAxis::FOLD_AXIS_VERTICAL;
        const int count = hw.hw_sensor_hinge_count > 0 ? hw.hw_sensor_hinge_count : 1;
        for (int i = 0; i < count; ++i) {
            auto* hinge = posture_caps->add_hinges();
            hinge->set_hinge_id(i);
            hinge->set_fold_axis(axis);
            hinge->set_min_angle_degrees(0.0F);
            hinge->set_max_angle_degrees(180.0F);
        }
    }
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
        PopulatePostureCapabilities(hw, sensor_caps->mutable_posture());
    }
}

void PopulateInputCapabilities(const HardwareConfig& hw, InputCapabilities* input_caps) {
    input_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    input_caps->add_supported_features(InputFeature::INPUT_FEATURE_TOUCH);
    input_caps->add_supported_features(InputFeature::INPUT_FEATURE_MULTI_TOUCH);
    input_caps->set_max_touch_point_count(10);
    if (hw.hw_keyboard) {
        input_caps->add_supported_features(InputFeature::INPUT_FEATURE_KEYBOARD);
    }
    // TODO(jansene): These should align with what we tell the system image.
    // i.e. features: VirtioMouse, VirtioDualModeMouse
    // Mouse pointer and relative mouse are intentionally not advertised yet.
    if (hw.hw_rotaryInput) {
        input_caps->add_supported_features(InputFeature::INPUT_FEATURE_ROTARY_ENCODER);
    }
}

void PopulateVmCapabilities(VmCapabilities* vm_caps) {
    vm_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    vm_caps->add_supported_operations(VmOperation::VM_OPERATION_PAUSE_RESUME);
    vm_caps->add_supported_operations(VmOperation::VM_OPERATION_HARD_RESET);
    vm_caps->add_supported_operations(VmOperation::VM_OPERATION_SHUTDOWN);
}

void PopulateBiometricsCapabilities(BiometricsCapabilities* bio_caps) {
    bio_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    bio_caps->set_supports_fingerprint(true);
}

void PopulateClipboardCapabilities(ClipboardCapabilities* clip_caps) {
    clip_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
}

void PopulateSystemCapabilities(SystemCapabilities* sys_caps) {
    sys_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    sys_caps->set_supports_reboot(true);
    PopulateBiometricsCapabilities(sys_caps->mutable_biometrics());
    PopulateClipboardCapabilities(sys_caps->mutable_clipboard());
}

void PopulateSnapshotCapabilities(SnapshotCapabilities* snap_caps) {
    snap_caps->set_access_mode(AccessMode::ACCESS_MODE_READ_WRITE);
    snap_caps->add_supported_features(SnapshotFeature::SNAPSHOT_FEATURE_LOCAL);
    // Cloud storage import/export is not wired up here; omitting the feature is
    // the signal, so there is nothing to set to false.
}

}  // namespace

CapabilitiesServiceImpl::CapabilitiesServiceImpl(const ::goldfish::avd::AvdUniverse& avd_universe)
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
    PopulateSnapshotCapabilities(reply->mutable_snapshot());

    return Status::OK;
}

}  // namespace goldfish::grpc::v2
