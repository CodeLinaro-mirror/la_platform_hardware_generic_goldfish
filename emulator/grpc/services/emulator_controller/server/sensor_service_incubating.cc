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

#include "android/emulation/control/incubating/sensor_service_incubating.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

SensorServiceIncubatingImpl::SensorServiceIncubatingImpl(::goldfish::sensors::PhysicalModel& pm)
        : mPhysicalModel(pm) {}

grpc::Status SensorServiceIncubatingImpl::getSensor(grpc::ServerContext* context,
                                                    const SensorValue* request,
                                                    SensorValue* reply) {
    const auto sensor = static_cast<::goldfish::sensors::AndroidSensor>(request->target() - 1);
    const auto data = mPhysicalModel.GetSensorData(sensor);

    reply->set_target(request->target());
    reply->set_status(SensorValue::SENSOR_STATE_OK);
    auto* reply_data = reply->mutable_value()->mutable_data();
    reply_data->Resize(data.value.size(), 0);
    std::copy(data.value.begin(), data.value.end(), reply_data->begin());
    return grpc::Status::OK;
}

grpc::Status SensorServiceIncubatingImpl::setSensor(grpc::ServerContext* context,
                                                    const SensorValue* request,
                                                    google::protobuf::Empty* reply) {
    const auto sensor = static_cast<::goldfish::sensors::AndroidSensor>(request->target() - 1);
    const auto& data = request->value().data();
    ::goldfish::sensors::SensorValue val(data.begin(), data.end());
    mPhysicalModel.SetSensorValue(sensor, val);
    return grpc::Status::OK;
}

::grpc::ServerWriteReactor<SensorValue>* SensorServiceIncubatingImpl::receiveSensorEvents(
        ::grpc::CallbackServerContext* context, const SensorValue* request) {
    return nullptr;
}

grpc::Status SensorServiceIncubatingImpl::setPhysicalModel(grpc::ServerContext* context,
                                                           const PhysicalModelValue* request,
                                                           google::protobuf::Empty* reply) {
    const auto parameter =
            static_cast<::goldfish::sensors::PhysicalParameter>(request->target() - 1);
    const auto& data = request->value().data();

    auto interpolation = ::PhysicalInterpolation::kStep;
    if (request->interpolation() == PhysicalModelValue::INTERPOLATION_SMOOTH) {
        interpolation = ::PhysicalInterpolation::kSmooth;
    }

    mPhysicalModel.SetPhysicalParameterValue(parameter, data.data(), data.size(), interpolation);
    return grpc::Status::OK;
}

grpc::Status SensorServiceIncubatingImpl::getPhysicalModel(grpc::ServerContext* context,
                                                           const PhysicalModelValue* request,
                                                           PhysicalModelValue* reply) {
    const auto parameter =
            static_cast<::goldfish::sensors::PhysicalParameter>(request->target() - 1);
    const size_t sz = ::goldfish::sensors::PhysicalModel::GetPhysicalParameterSize(parameter);

    auto value_type = ::ParameterValueType::kCurrent;
    switch (request->value_type()) {
    case PhysicalModelValue::PARAMETER_VALUE_TYPE_TARGET:
        value_type = ::ParameterValueType::kTarget;
        break;
    case PhysicalModelValue::PARAMETER_VALUE_TYPE_CURRENT:
        value_type = ::ParameterValueType::kCurrent;
        break;
    case PhysicalModelValue::PARAMETER_VALUE_TYPE_CURRENT_NO_AMBIENT_MOTION:
        value_type = ::ParameterValueType::kCurrentNoAmbientMotion;
        break;
    case PhysicalModelValue::PARAMETER_VALUE_TYPE_DEFAULT:
        value_type = ::ParameterValueType::kDefault;
        break;
    default:
        break;
    }

    std::vector<float> data(sz);
    mPhysicalModel.GetPhysicalParameterValue(parameter, data.data(), sz, value_type);

    reply->set_target(request->target());
    reply->set_status(PhysicalModelValue::PHYSICAL_STATE_VALUE_OK);
    auto* reply_data = reply->mutable_value()->mutable_data();
    reply_data->Resize(sz, 0);
    std::copy(data.begin(), data.end(), reply_data->begin());
    return grpc::Status::OK;
}

::grpc::ServerWriteReactor<PhysicalModelValue>*
SensorServiceIncubatingImpl::receivePhysicalModelEvents(::grpc::CallbackServerContext* context,
                                                        const PhysicalModelValue* request) {
    return nullptr;
}

::grpc::ServerWriteReactor<PhysicalStateEvent>*
SensorServiceIncubatingImpl::receivePhysicalStateEvents(::grpc::CallbackServerContext* context,
                                                        const google::protobuf::Empty* request) {
    return nullptr;
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android
