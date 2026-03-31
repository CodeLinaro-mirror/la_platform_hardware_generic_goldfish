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

#include "emulator_controller.grpc.pb.h"
#include "goldfish/avd_universe/battery/battery_state.h"

namespace android::emulation::control {

using ::google::protobuf::Empty;
using grpc::Status;

/**
 * @brief Implements the Battery-related gRPC APIs.
 *
 * This class provides an interface for controlling the emulated battery device
 * through gRPC. It interacts with the underlying `ObservableBattery` to set and
 * retrieve battery state information.
 */
class BatteryServiceImpl {
  public:
    explicit BatteryServiceImpl(::goldfish::avd_universe::battery::ObservableBattery& battery)
            : observable_batttery_(battery) {}

    /**
     * @brief Sets the battery state.
     *
     * @param request The battery state to set.
     * @return A gRPC status indicating the success or failure of the operation.
     */
    Status setBattery(const BatteryState& request);

    /**
     * @brief Retrieves the current battery state.
     *
     * @param reply The battery state containing the last known battery information.
     * @return A gRPC status indicating the success or failure of the operation.
     */
    Status getBattery(BatteryState* reply);

  private:
    ::goldfish::avd_universe::battery::ObservableBattery& observable_batttery_;
};

}  // namespace android::emulation::control
