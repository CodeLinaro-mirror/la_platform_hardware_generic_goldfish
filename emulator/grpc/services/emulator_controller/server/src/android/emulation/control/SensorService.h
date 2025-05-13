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

#pragma once

#include <grpcpp/grpcpp.h>

#include "goldfish/devices/connector_registry.h"
#include "hardware/generic/goldfish/emulator/grpc/services/emulator_controller/proto/emulator_controller.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::devices::ConnectorRegistry;
using grpc::ServerContext;
using grpc::Status;

/**
 * @brief Implements the sensor-related gRPC APIs.
 *
 * This class provides an implementation for setting and retrieving
 * sensor values via gRPC. It interacts with the underlying sensor
 * device through the `ConnectorRegistry`.
 */
class SensorServiceImpl {
  public:
    SensorServiceImpl(ConnectorRegistry* connectorRegistry);

    grpc::Status setSensor(ServerContext* context, const SensorValue* request,
                           ::google::protobuf::Empty* reply);

    grpc::Status getSensor(ServerContext* context, const SensorValue* request, SensorValue* reply);

  private:
    ConnectorRegistry* mRegistry;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
