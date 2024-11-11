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

#include <chrono>
#include <vector>

#include "absl/status/statusor.h"

#include "aemu/base/async/Looper.h"
#include "android/emulation/control/utils/CallbackEventSupport.h"
#include "android/goldfish/config/avd.h"
#include "android/physics/Sensors.h"
#include "goldfish/devices/cable/cable.h"

namespace goldfish::devices::sensor {

using android::base::Looper;
using android::emulation::control::EventChangeSupport;
using android::emulation::control::WithCallbacks;
using android::goldfish::Avd;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

using SensorData = std::vector<float>;

// A Qemud based sensor emulator.
class ISensorDevice : public IPlug, public WithCallbacks<EventChangeSupport, AndroidSensor> {
  public:
    ~ISensorDevice() override {}

    // Name under which you should register this in qemud
    static constexpr std::string_view name = "sensors";

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
    virtual absl::Status overrideSensor(AndroidSensor sensor_id, const SensorData& data) = 0;

    virtual bool isSensorEnabled(AndroidSensor sensor_id) = 0;

    virtual std::chrono::microseconds getSensorTimeOffset() = 0;

    virtual std::chrono::milliseconds getSensorDelayMs() = 0;

    // Registers the sensor device with qemu
    static std::shared_ptr<ISensorDevice> create(SocketPtr socket, Avd* avd, Looper* looper);
};
}  // namespace goldfish::devices::sensor