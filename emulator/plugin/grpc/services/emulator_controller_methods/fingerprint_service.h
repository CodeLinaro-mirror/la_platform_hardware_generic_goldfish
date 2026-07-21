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
#include "goldfish/avd_universe/fingerprint/fingerprint_sensor.h"

namespace android::emulation::control {

using ::goldfish::avd_universe::fingerprint::ObservableFingerprintSensor;
using ::google::protobuf::Empty;
using grpc::Status;

/**
 * @brief Implements the Fingerprint-related gRPC APIs.
 *
 * This class provides an interface for controlling the emulated fingerprint sensor
 * through gRPC. It interacts with the underlying `ObservableFingerprintSensor`.
 */
class FingerprintServiceImpl {
  public:
    explicit FingerprintServiceImpl(ObservableFingerprintSensor& sensor)
            : fingerprint_sensor_(sensor) {}

    /**
     * @brief Sends a fingerprint touch/remove event.
     *
     * @param request The fingerprint touch state and ID.
     * @return A gRPC status indicating success.
     */
    Status sendFingerprint(const Fingerprint& request);

  private:
    ObservableFingerprintSensor& fingerprint_sensor_;
};

}  // namespace android::emulation::control
