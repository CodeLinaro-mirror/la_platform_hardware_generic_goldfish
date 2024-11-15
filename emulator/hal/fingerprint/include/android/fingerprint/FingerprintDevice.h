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

#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry.h"

namespace goldfish::devices::fingerprint {

using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

using namespace std::string_view_literals;
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
class IFingerprintDevice : public IPlug {
  public:
    virtual ~IFingerprintDevice() override {}

    /**
     * @brief QEMU service name for the fingerprint device.
     */
    static constexpr std::string_view serviceName = "fingerprintlisten"sv;

    /**
     * @brief Simulates a finger touch on the sensor.
     *
     * @param id An identifier for the touch event.  This could be used to
     *            distinguish between different fingers.
     */
    virtual void touch(int id) = 0;

    /**
     * @brief Simulates the release of a finger from the sensor.
     */
    virtual void release() = 0;

    /**
     * @brief Registers the fingerprint device with the connector registry.
     *
     * This function registers the fingerprint device with the provided
     * \p registry instance, making it available for connection through
     * the qemud pipe.
     *
     * @param registry  The connector registry instance.
     */
    static void registerDevice(IConnectorRegistry* registry);
};
}  // namespace goldfish::devices::fingerprint