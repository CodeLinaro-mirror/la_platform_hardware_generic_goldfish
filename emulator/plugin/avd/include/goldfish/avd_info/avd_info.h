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

namespace goldfish::devices::multidisplay {
class MultiDisplayDevice;
}

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
 * (in this case `GetAvd()` should be adjusted to
 * receive some form of an AVD id).
 *
 * The instance of this type is available between
 * the `avd_info_realize` and `avd_info_unrealize` events.
 */
struct AvdUniverse {
    const AvdProperties& Props() const { return *props_; }

    avd_universe::battery::ObservableBattery& GetBattery() { return battery_; }
    avd_universe::clipboard::ClipboardChannel& GetClipboardChannel() { return clipboard_channel_; }
    avd_universe::fingerprint::ObservableFingerprintSensor& GetFingerprintSensor() {
        return fingerprint_sensor_;
    }
    avd_universe::grpc::GrpcNotificationEventSource& GetGrpcNotificationChannel() {
        return grpc_notification_event_source_;
    }
    avd_universe::guest_status::GuestStatus& GetGuestStatus() { return guest_status_; }
    avd_universe::gps::ObservableLocation& GetLocation() { return location_; }
    sensors::PhysicalModel& GetSensorsPhysicalModel() { return sensors_physical_model_; }

    void SetActiveMultiDisplayDevice(
            std::shared_ptr<devices::multidisplay::MultiDisplayDevice> device);
    std::shared_ptr<devices::multidisplay::MultiDisplayDevice> GetActiveMultiDisplayDevice();

    explicit AvdUniverse(std::unique_ptr<AvdProperties> props);
    AvdUniverse(const AvdUniverse&) = delete;
    AvdUniverse(AvdUniverse&&) = delete;
    AvdUniverse& operator=(const AvdUniverse&) = delete;
    AvdUniverse& operator=(AvdUniverse&&) = delete;

  private:
    const std::unique_ptr<const AvdProperties> props_;

    mutable absl::Mutex device_mutex_;
    std::shared_ptr<devices::multidisplay::MultiDisplayDevice> active_multi_display_device_
            ABSL_GUARDED_BY(device_mutex_);

    avd_universe::battery::ObservableBattery battery_;
    avd_universe::clipboard::ClipboardChannel clipboard_channel_;
    avd_universe::fingerprint::ObservableFingerprintSensor fingerprint_sensor_;
    avd_universe::grpc::GrpcNotificationEventSource grpc_notification_event_source_;
    avd_universe::guest_status::GuestStatus guest_status_;
    avd_universe::gps::ObservableLocation location_;
    sensors::PhysicalModel sensors_physical_model_;
};

AvdUniverse& GetAvd();

}  // namespace goldfish::avd_info
