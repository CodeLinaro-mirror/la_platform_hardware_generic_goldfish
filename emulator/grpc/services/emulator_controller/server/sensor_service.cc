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
#include "emulator/grpc/services/emulator_controller/server/sensor_service.h"

#include "android/emulation/control/absl_status_translate.h"

namespace android {
namespace emulation {
namespace control {

using GoldfishSensor = ::goldfish::sensors::AndroidSensor;
using GoldfishSensorData = ::goldfish::sensors::SensorData;
using GoldfishSensorValue = ::goldfish::sensors::SensorValue;

grpc::Status SensorServiceImpl::setSensor(const SensorValue& request) {
    const GoldfishSensor sensor = static_cast<GoldfishSensor>(request.target());
    const auto& requestData = request.value().data();
    mPhysicalModel.SetSensorValue(sensor,
                                  GoldfishSensorValue(requestData.begin(), requestData.end()));
    return Status::OK;
}

grpc::Status SensorServiceImpl::getSensor(const SensorValue& request, SensorValue* reply) {
    const GoldfishSensor sensor = static_cast<GoldfishSensor>(request.target());
    const GoldfishSensorData sd = mPhysicalModel.GetSensorData(sensor);
    const GoldfishSensorValue& val = sd.value;

    reply->set_target(request.target());
    *reply->mutable_value()->mutable_data() = {val.begin(), val.end()};

    return Status::OK;
}

}  // namespace control
}  // namespace emulation
}  // namespace android
