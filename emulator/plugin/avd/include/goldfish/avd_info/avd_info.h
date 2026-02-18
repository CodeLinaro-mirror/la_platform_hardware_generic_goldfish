// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#pragma once

#include <filesystem>
#include <string>

#include "android/goldfish/device_type.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/avd_universe/battery/battery_state.h"
#include "goldfish/avd_universe/clipboard/clipboard_data.h"
#include "goldfish/avd_universe/fingerprint/fingerprint_sensor.h"
#include "goldfish/avd_universe/gps/location.h"
#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"
#include "goldfish/avd_universe/guest_status/guest_status.h"
#include "goldfish/sensors/physical_model.h"

namespace goldfish::avd_info {

struct AvdProperties {
    int32_t serial_number{0};
    int32_t adb_port{0};
    std::string avd_name;
    std::string avd_id;
    std::string avd_abi;
    int32_t avd_api{0};
    android::goldfish::DeviceType avd_type{android::goldfish::DeviceType::kUnknown};
    std::filesystem::path avd_content_path;
    std::string build_sdk;
    std::string build_id;
    std::string build_flavour;
    int32_t quit_after_boot_timeout_seconds{0};

    android::goldfish::HardwareConfig hw_config;
};

/**
 * @brief This struct represents the whole AVD state.
 *
 * All AVD specific data should live here. This
 * explicitly describes the data lifetime and also
 * allows running several AVDs simultaneously
 * (in this case `getAvd()` should be adjusted to
 * receive some form of an AVD id).
 *
 * The instance of this type is available between
 * the `avd_info_realize` and `avd_info_unrealize` events.
 */
struct AvdUniverse {
    const AvdProperties& props() const { return *mProps; }

    avd_universe::battery::ObservableBattery& getBattery() { return mBattery; }
    avd_universe::clipboard::ClipboardChannel& getClipboardChannel() { return mClipboardChannel; }
    avd_universe::fingerprint::ObservableFingerprintSensor& getFingerprintSensor() {
        return mFingerprintSensor;
    }
    avd_universe::grpc::GrpcNotificationEventSource& getGrpcNotificationChannel() {
        return mGrpcNotificationEventSource;
    }
    avd_universe::guest_status::GuestStatus& getGuestStatus() { return mGuestStatus; }
    avd_universe::gps::ObservableLocation& getLocation() { return mLocation; }
    sensors::PhysicalModel& getSensorsPhysicalModel() { return mSensorsPhysicalModel; }

    AvdUniverse(std::unique_ptr<AvdProperties> props);
    AvdUniverse(const AvdUniverse&) = delete;
    AvdUniverse(AvdUniverse&&) = delete;
    AvdUniverse& operator=(const AvdUniverse&) = delete;
    AvdUniverse& operator=(AvdUniverse&&) = delete;

  private:
    const std::unique_ptr<const AvdProperties> mProps;

    avd_universe::battery::ObservableBattery mBattery;
    avd_universe::clipboard::ClipboardChannel mClipboardChannel;
    avd_universe::fingerprint::ObservableFingerprintSensor mFingerprintSensor;
    avd_universe::grpc::GrpcNotificationEventSource mGrpcNotificationEventSource;
    avd_universe::guest_status::GuestStatus mGuestStatus;
    avd_universe::gps::ObservableLocation mLocation;
    sensors::PhysicalModel mSensorsPhysicalModel;
};

AvdUniverse& getAvd();

}  // namespace goldfish::avd_info
