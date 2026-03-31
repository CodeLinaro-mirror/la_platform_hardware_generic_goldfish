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

#pragma once

#include <grpcpp/grpcpp.h>

#include "goldfish/sensors/physical_model.h"
#include "sensor_service.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

/**
 * @brief Implements the incubating sensor-related gRPC APIs.
 *
 * This class provides an implementation for the incubating SensorService,
 * which allows for fine-grained control and monitoring of the emulator's
 * physical model and sensors.
 *
 * It supports:
 * - Direct access to physical model parameters (position, rotation, etc.)
 * - Direct access to individual Android sensor values.
 * - Streaming events for physical state transitions.
 * - Streaming updates for physical model parameter changes.
 */
class SensorServiceIncubatingImpl final
        : public SensorService::WithCallbackMethod_receiveSensorEvents<
                  SensorService::WithCallbackMethod_receivePhysicalModelEvents<
                          SensorService::WithCallbackMethod_receivePhysicalStateEvents<
                                  SensorService::Service>>> {
  public:
    /**
     * @brief Constructs a new SensorServiceIncubatingImpl.
     * @param pm The physical model instance to interact with.
     */
    SensorServiceIncubatingImpl(::goldfish::sensors::PhysicalModel& pm);

    /**
     * @brief Retrieves the current value of a specific sensor.
     * @param context gRPC server context.
     * @param request Contains the target sensor to query.
     * @param reply Filled with the current sensor value and status.
     * @return grpc::Status::OK on success.
     */
    grpc::Status getSensor(grpc::ServerContext* context, const SensorValue* request,
                           SensorValue* reply) override;

    /**
     * @brief Overrides the value of a specific sensor.
     * @param context gRPC server context.
     * @param request Contains the target sensor and the new value to set.
     * @param reply Empty response.
     * @return grpc::Status::OK on success.
     */
    grpc::Status setSensor(grpc::ServerContext* context, const SensorValue* request,
                           google::protobuf::Empty* reply) override;

    /**
     * @brief Streams sensor events (Not yet implemented).
     */
    ::grpc::ServerWriteReactor<SensorValue>* receiveSensorEvents(
            ::grpc::CallbackServerContext* context, const SensorValue* request) override;

    /**
     * @brief Updates a specific physical model parameter.
     * @param context gRPC server context.
     * @param request Contains the target parameter, data, and interpolation type.
     * @param reply Empty response.
     * @return grpc::Status::OK on success.
     */
    grpc::Status setPhysicalModel(grpc::ServerContext* context, const PhysicalModelValue* request,
                                  google::protobuf::Empty* reply) override;

    /**
     * @brief Retrieves the value of a specific physical model parameter.
     * @param context gRPC server context.
     * @param request Contains the target parameter and the type of value to retrieve (current,
     * target, etc.).
     * @param reply Filled with the parameter data and status.
     * @return grpc::Status::OK on success.
     */
    grpc::Status getPhysicalModel(grpc::ServerContext* context, const PhysicalModelValue* request,
                                  PhysicalModelValue* reply) override;

    /**
     * @brief Streams updates for a specific physical model parameter.
     *
     * This method allows clients to monitor changes to a parameter (like position)
     * as it transitions between states.
     *
     * @param context gRPC callback server context.
     * @param request Specifies the target parameter and the type of values to stream.
     * @return A reactor that handles the event stream.
     */
    ::grpc::ServerWriteReactor<PhysicalModelValue>* receivePhysicalModelEvents(
            ::grpc::CallbackServerContext* context, const PhysicalModelValue* request) override;

    /**
     * @brief Streams physical state transition events.
     *
     * Events include state changing, target reached, and stabilization.
     *
     * @param context gRPC callback server context.
     * @param request Empty request.
     * @return A reactor that handles the event stream.
     */
    ::grpc::ServerWriteReactor<PhysicalStateEvent>* receivePhysicalStateEvents(
            ::grpc::CallbackServerContext* context,
            const google::protobuf::Empty* request) override;

  private:
    ::goldfish::sensors::PhysicalModel& mPhysicalModel;
};

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android
