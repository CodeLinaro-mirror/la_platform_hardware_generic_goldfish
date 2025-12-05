// Copyright (C) 2024 The Android Open Source Project
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
#include "android/emulation/control/SensorService.h"

#include "android/grpc/utils/absl_status_translate.h"
#include "goldfish/devices/sensor/SensorDevice.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::devices::ConnectorRegistry;
using ::goldfish::devices::sensor::AndroidSensor;
using ::goldfish::devices::sensor::ISensorDevice;
using GoldfishSensorValue = ::goldfish::devices::sensor::SensorValue;

SensorServiceImpl::SensorServiceImpl(ConnectorRegistry* connectorRegistry)
        : mRegistry(connectorRegistry) {}

grpc::Status SensorServiceImpl::setSensor(const SensorValue& request) {
    auto weak = mRegistry->activeDevice<ISensorDevice>();
    if (auto sensor = weak.lock()) {
        GoldfishSensorValue value(request.value().data().begin(), request.value().data().end());
        auto status = sensor->overrideSensor(static_cast<AndroidSensor>(request.target()), value);
        return abslStatusToGrpcStatus(status);
    }
    return Status(grpc::StatusCode::UNAVAILABLE, "No active sensor device");
}

grpc::Status SensorServiceImpl::getSensor(const SensorValue& request, SensorValue* reply) {
    auto weak = mRegistry->activeDevice<ISensorDevice>();
    if (auto sensor = weak.lock()) {
        auto statusOrData = sensor->getSensorData(static_cast<AndroidSensor>(request.target()));
        if (!statusOrData.ok()) {
            return abslStatusToGrpcStatus(statusOrData.status());
        }

        const GoldfishSensorValue& val = statusOrData->value;

        reply->set_target(request.target());
        *reply->mutable_value()->mutable_data() = {val.begin(), val.end()};

        return Status::OK;
    }
    return Status(grpc::StatusCode::UNAVAILABLE, "No active sensor device");
}

}  // namespace control
}  // namespace emulation
}  // namespace android
