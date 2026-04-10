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
#pragma once

#include <string_view>

#include "goldfish/avd_universe/fingerprint/fingerprint_sensor.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/internal/hal_plug.h"

namespace goldfish::devices::fingerprint {

using namespace std::string_view_literals;
using goldfish::async::EventLoop;
using goldfish::avd_universe::fingerprint::ObservableFingerprintSensor;

/**
 * @brief Interface for emulating a fingerprint sensor in the Android emulator.
 *
 * This interface provides methods to simulate fingerprint touch events.  It allows
 * clients to interact with the emulated sensor by sending touch and release signals,
 * enabling testing and development of fingerprint-based functionalities. The
 * fingerprint sensor is registered as a QEMUD service under the name "fingerprintlisten".
 *
 * The guest HAL lives in device/generic/goldfish/fingerprint/fingerprint.c
 */
class IFingerprintDevice : public HalPlug {
  public:
    /**
     * @brief QEMU service name for the fingerprint device.
     */
    static constexpr std::string_view kServiceName = "fingerprintlisten"sv;

    /**
     * @brief Registers the fingerprint device with the connector registry.
     *
     * This function registers the fingerprint device with the provided
     * \p registry instance, making it available for connection through
     * the qemud pipe.
     *
     * @param registry  The connector registry instance.
     * @param client_loop The event loop for client-side operations.
     * @param qemuLoop The event loop for QEMU-side operations.
     */
    static void RegisterDevice(ObservableFingerprintSensor* sensor, IConnectorRegistry* registry,
                               EventLoop* client_loop, EventLoop* qemu_loop);
};
}  // namespace goldfish::devices::fingerprint
