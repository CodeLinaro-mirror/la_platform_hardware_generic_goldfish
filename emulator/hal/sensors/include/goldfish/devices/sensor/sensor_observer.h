// Copyright 2025 The Android Open Source Project
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

#include "goldfish/devices/connector_registry_impl.h"
#include "goldfish/devices/sensor/SensorDevice.h"

namespace goldfish::devices::sensor {

/**
 * @brief Observes changes in sensor data for a specific Android sensor.
 *
 * The SensorObserver class allows you to monitor a particular Android sensor
 * and receive notifications when its data changes. It internally compares
 * the current sensor data with the previously observed data and triggers an
 * event if a change is detected.
 *
 * @tparam SensorData The type of data representing the sensor values (e.g., std::vector<float>).
 *
 * @note
 * The SensorObserver relies on the `ISensorDevice` to provide sensor data and
 * to notify it when sensor events occur.
 */
class SensorObserver : public CallbackEventSource<SensorData> {
  public:
    /**
     * @brief Constructs a SensorObserver for a specific sensor.
     *
     * @param registry Registry used to fetch the ISensorDevice
     * @param id The AndroidSensor ID to observe.
     */
    SensorObserver(ConnectorRegistry* registry, AndroidSensor id);
    ~SensorObserver();

  private:
    void registerDevice(std::weak_ptr<ISensorDevice> device);
    void forwardEvent(const AndroidSensor sensorId);
    SensorData mOld;  ///< The previously observed sensor data.
    DeviceRegistrationListener<ISensorDevice>
            mDeviceListener;               ///< Listener for device registration events.
    std::weak_ptr<ISensorDevice> mDevice;  ///< The ISensorDevice being observed.
    const AndroidSensor mId;
    CallbackId mCallbackId =
            1234567890;  ///< The ID of the registered callback in the ISensorDevice.
};

}  // namespace goldfish::devices::sensor
