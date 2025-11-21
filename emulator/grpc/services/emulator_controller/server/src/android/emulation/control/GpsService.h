// Copyright (C) 2025 The Android Open Source Project
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

#include "emulator_controller.grpc.pb.h"
#include "goldfish/devices/connector_registry.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::devices::ConnectorRegistry;
using ::google::protobuf::Empty;
using grpc::ServerContext;
using grpc::Status;

/**
 * @brief Implements the GPS-related gRPC APIs.
 *
 * This class provides an interface for controlling the emulated GPS device
 * through gRPC. It interacts with the underlying `IGpsDevice` to set and
 * retrieve GPS location information.
 */
class GpsServiceImpl {
  public:
    /**
     * @brief Constructs a new GpsServiceImpl.
     *
     * @param connectorRegistry The connector registry used to access the
     *        `IGpsDevice` instance.  This registry provides access to
     *        the emulated hardware devices.
     */
    explicit GpsServiceImpl(ConnectorRegistry* connectorRegistry) : mRegistry(connectorRegistry) {}

    /**
     * @brief Destroys the GpsServiceImpl.
     */
    ~GpsServiceImpl() = default;  // Use default destructor

    /**
     * @brief Sets the GPS state.
     *
     * This method sets the location of the emulated GPS device.
     * The update frequency is limited by the Android framework, which typically
     * samples GPS data at 1Hz.  Changes to the GPS position might not be
     * immediately reflected in the device due to this sampling rate.  The
     * provided `GpsState` message contains the new location information.
     *
     * @param context The server context for the gRPC call.
     * @param request The GPS state to set, containing the desired location
     *        information.  This is a pointer to a const `GpsState` object.
     * @param reply An empty message used for the gRPC response.  This is a
     *        pointer to an empty `google::protobuf::Empty` object.
     * @return A gRPC status indicating the success or failure of the operation.
     *         Possible status codes include `grpc::OK` for success, and other
     *         codes (e.g., `grpc::INVALID_ARGUMENT`) for errors.
     */
    Status setGps(const GpsState& request);

    /**
     * @brief Retrieves the current GPS state.
     *
     * This method retrieves the last GPS state set via `setGps`.
     * Note that the returned GPS state may not represent
     * the real-time location due to the Android framework's GPS sampling rate
     * (usually 1Hz).
     *
     * @param context The server context for the gRPC call.
     * @param request An empty message used for the gRPC request. This is a
     *        pointer to a const `google::protobuf::Empty` object.
     * @param reply The GPS state containing the last known location information.
     *        This is a pointer to a `GpsState` object where the retrieved
     *        information will be stored.
     * @return A gRPC status indicating the success or failure of the operation.
     *         Possible status codes include `grpc::OK` for success, and other
     *         codes (e.g., `grpc::UNAVAILABLE`) if the GPS state is not available.
     */
    Status getGps(GpsState* reply);

  private:
    ConnectorRegistry* mRegistry;  ///< The connector registry.
};

}  // namespace control
}  // namespace emulation
}  // namespace android