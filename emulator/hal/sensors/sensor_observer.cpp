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

#include "goldfish/devices/sensor/sensor_observer.h"

namespace goldfish::devices::sensor {

SensorObserver::SensorObserver(ConnectorRegistry* registry, AndroidSensor id)
        : mDeviceListener(registry), mId(id) {
    mDeviceListener.addCallback(
            [this](std::weak_ptr<ISensorDevice> device) { registerDevice(device); });
}

void SensorObserver::registerDevice(std::weak_ptr<ISensorDevice> weakSensor) {
    if (auto sensor = weakSensor.lock()) {
        mDevice = sensor;
        mCallbackId =
                sensor->addCallback([this](AndroidSensor sensorId) { forwardEvent(sensorId); });
    }
}

void SensorObserver::forwardEvent(const AndroidSensor sensorId) {
    if (sensorId != mId) {
        return;
    }
    auto device = mDevice.lock();
    if (!device) {
        return;
    }
    auto data = device->getSensorData(sensorId);
    if (!data.ok()) {
        return;
    }

    if (mOld != data.value()) {
        mOld = data.value();
        SensorObserver::fireEvent(mOld);
    }
}

SensorObserver::~SensorObserver() {
    if (auto sensor = mDevice.lock()) {
        sensor->removeCallback(mCallbackId);
    }
}

}  // namespace goldfish::devices::sensor
