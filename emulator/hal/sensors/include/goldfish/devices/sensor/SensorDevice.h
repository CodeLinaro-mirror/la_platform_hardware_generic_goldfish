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

#include <memory>
#include <vector>

#include "absl/status/statusor.h"

#include "aemu/base/events/EventSources.h"
#include "android/goldfish/config/device_type.h"
#include "android/goldfish/config/hardware_config.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/hal/plug/HalPlug.h"
#include "goldfish/physics/Rotation.h"
#include "goldfish/sensors/AndroidSensor.h"
#include "goldfish/sensors/sensor_data.h"

namespace android::base {
class IClock;
}

namespace goldfish::devices::sensor {

using android::base::eventing::CallbackEventSource;
using goldfish::async::EventLoop;
using goldfish::physics::Rotation;
using goldfish::sensors::AndroidSensor;
using goldfish::sensors::SensorData;
using goldfish::sensors::SensorValue;

using namespace std::string_view_literals;

// A Qemud based sensor emulator.
class ISensorDevice : public HalPlug,
                      public CallbackEventSource<AndroidSensor>,
                      public std::enable_shared_from_this<ISensorDevice> {
  public:
    ~ISensorDevice() override {}

    // Name under which you should register this in qemud
    static constexpr std::string_view serviceName = "sensors"sv;

    /**
     * @brief Retrieves sensor data for the specified Android sensor.
     *
     * @param sensor_id The ID of the Android sensor to read from.
     *
     * @return StatusOr<SensorData> On success, returns a vector of sensor values.
     *         Returns errors in the following cases:
     *         - InvalidArgumentError if sensor_id is out of valid range
     *         - UnavailableError if the requested sensor is disabled
     */
    virtual absl::StatusOr<SensorData> getSensorData(AndroidSensor sensor_id) = 0;

    /**
     * @brief Overrides the current sensor values with provided data and triggers a sensor event.
     *
     * @param sensor_id The ID of the Android sensor to override. Must be in range [0, MAX_SENSORS).
     * @param data Vector containing the new sensor values to set.
     *
     * @return absl::Status Returns Status::OK on success.
     *         Returns errors in the following cases:
     *         - InvalidArgumentError if sensor_id is out of valid range
     *         - UnavailableError if the requested sensor is disabled
     *
     * @details For hinge angle sensors (ANDROID_SENSOR_HINGE_ANGLE0/1/2), applies smooth
     *          interpolation when setting the physical parameter values. For all other sensors,
     *          directly sets the sensor values.
     *
     * @note Triggers a sensor event after successfully setting the new values.
     */
    virtual absl::Status overrideSensor(AndroidSensor sensor_id, const SensorValue& val) = 0;

    /**
     * @brief Retrieves the device's current rotation based on accelerometer data.
     *
     * This method calculates the device's rotation by querying the accelerometer
     * sensor (ANDROID_SENSOR_ACCELERATION) and analyzing its output.  The returned
     * `Rotation` object provides both a coarse-grained representation of the
     * rotation (as a `Rotation::SkinRotation` enum value: PORTRAIT, LANDSCAPE,
     * REVERSE_PORTRAIT, or REVERSE_LANDSCAPE) and the raw accelerometer readings
     * along the x, y, and z axes.
     *
     * The coarse rotation is determined by comparing the normalized accelerometer
     * vector with known reference vectors for each of the four standard orientations.
     *
     * @return absl::StatusOr<Rotation> An `absl::StatusOr` object containing the
     *         derived rotation information.
     *         - If successful, the `value()` method of the returned object will
     *           contain the `Rotation` object.
     *         - If an error occurs (e.g., accelerometer data unavailable), the
     *           returned object will contain an error status.  Check the status
     *           using the `ok()` method.
     */
    virtual absl::StatusOr<Rotation> getDeviceRotation() = 0;

    /**
     * @brief Registers the sensor device with the connector registry.
     *
     * This function registers the sensor device with the provided
     * `IConnectorRegistry` instance, making it available for connection
     * through the qemud pipe.  The provided `Avd` object supplies
     * configuration information for the sensor device, while the `Looper`
     * instance manages the event loop for asynchronous operations.
     *
     * @param registry The `IConnectorRegistry` instance to register with.
     * @param avd The `Avd` object containing the AVD configuration.
     * @param looper The `Looper` instance to use for asynchronous operations.
     *
     * @note The `avd` and `looper` objects are expected to remain valid for
     * the lifetime of the registry.  Their lifecycles should be managed
     * externally to ensure they outlive the registry.
     */
    static void registerDevice(IConnectorRegistry* registry, android::goldfish::DeviceType avd_type,
                               int avd_api, const android::goldfish::HardwareConfig& hw,
                               EventLoop* clientLoop, EventLoop* qemuLoop);
    // Test seam
    static void registerDevice(IConnectorRegistry* registry, android::goldfish::DeviceType avd_type,
                               int avd_api, const android::goldfish::HardwareConfig& hw,
                               EventLoop* clientLoop, EventLoop* qemuLoop,
                               ::android::base::IClock* clock);
};

}  // namespace goldfish::devices::sensor
