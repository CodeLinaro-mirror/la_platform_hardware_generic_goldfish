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
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet.");
}

grpc::Status SensorServiceIncubatingImpl::setSensor(grpc::ServerContext* context,
                                                    const SensorValue* request,
                                                    google::protobuf::Empty* reply) {
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet.");
}

::grpc::ServerWriteReactor<SensorValue>* SensorServiceIncubatingImpl::receiveSensorEvents(
        ::grpc::CallbackServerContext* context, const SensorValue* request) {
    return nullptr;
}

grpc::Status SensorServiceIncubatingImpl::setPhysicalModel(grpc::ServerContext* context,
                                                           const PhysicalModelValue* request,
                                                           google::protobuf::Empty* reply) {
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet.");
}

grpc::Status SensorServiceIncubatingImpl::getPhysicalModel(grpc::ServerContext* context,
                                                           const PhysicalModelValue* request,
                                                           PhysicalModelValue* reply) {
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet.");
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
