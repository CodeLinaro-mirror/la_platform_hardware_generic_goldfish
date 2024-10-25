
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
#include <goldfish/vsock/listen.h>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "aemu/base/Compiler.h"
#include "goldfish/devices/Connector.h"
#include "goldfish/devices/PingTopic.h"

namespace goldfish {
namespace devices {

using goldfish::vsock::HostPortListener;

/**
 * @brief A registry for managing and listening for connections to virtual devices.
 *
 * The `ConnectorRegistry` class facilitates the registration and connection of
 * virtual devices, primarily within the QEMU environment. It allows QEMU devices
 * to register themselves during the initial registration phase. Subsequently,
 * during the QEMU launch phase, the `listen` call can be made to activate these
 * registered devices and make them accessible to the guest system.
 *
 * @see goldfish/devices/Connector.h for details on the underlying protocol.
 */
class ConnectorRegistry {
  public:
    /**
     * @brief Function signature for starting a listener.
     *
     * @param listener The `HostPortListener` to be used for accepting connections.
     * @return `true` if the listener was started successfully, `false` otherwise.
     */
    using ListenFn = std::function<bool(HostPortListener)>;

    /**
     * @brief Constructs a `ConnectorRegistry` with a default ping topic
     */

    ConnectorRegistry();
    DISALLOW_COPY_AND_ASSIGN(ConnectorRegistry);

    /**
     * @brief Constructs a `ConnectorRegistry` object.
     *
     * @param pingTopic The `PingTopic` instance to be used by the connectors.
     */
    explicit ConnectorRegistry(std::shared_ptr<PingTopic> pingTopic);

    /**
     * @brief Starts listening for connections on the specified vsock port.
     *
     * This creates a vsock server socket on the host side, allowing the guest
     * to connect to this port and access the registered virtual devices.
     *
     * @param port The vsock port number to listen on.
     * @return `true` if the listener was started successfully, `false` otherwise.
     */
    bool listen(int port);

    /**
     * @brief Starts listening for connections using a custom listen function.
     *
     * This method is mainly used for testing.
     *
     * @param startListening The function to be used for starting the listener.
     * @return `true` if the listener was started successfully, `false` otherwise.
     */
    bool listen(ListenFn startListening);

    /**
     * @brief Registers a QEMU device with the registry.
     *
     * This method registers devices that use the older "qemud" protocol, which
     * has some differences compared to the standard protocol used by
     * `registerDevice()`. The "qemud" protocol involves a specific data format
     * and handshake mechanism.
     *
     * @see https://android.googlesource.com/platform/external/qemu/+/master/docs/ANDROID-QEMUD.TXT
     *      for a detailed description of the "qemud" protocol.
     *
     * @param name The name of the device.
     * @param factory The factory function for creating the device.
     * @return `true` if the device was registered successfully, `false` otherwise.
     */
    bool registerQemuDevice(std::string name, Connector::DeviceFactory factory);

    /**
     * @brief Registers a device with the registry.
     *
     * @param name The name of the device.
     * @param factory The factory function for creating the device.
     * @return `true` if the device was registered successfully, `false` otherwise.
     */
    bool registerDevice(std::string name, Connector::DeviceFactory factory);

    static ConnectorRegistry& defaultRegistry();

  private:
    std::shared_ptr<PingTopic> mPingTopic;
    bool mAcceptingRegistries;
    std::mutex mEntriesMutex;
    absl::flat_hash_map<std::string, Connector::DeviceFactory> mEntries;
    std::vector<Connector::DeviceEntry> mDevices;
};
}  // namespace devices
}  // namespace goldfish
