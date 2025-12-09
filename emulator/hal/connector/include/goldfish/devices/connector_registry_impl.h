
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

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/connector.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/device_entry.h"

namespace goldfish {
namespace devices {

using HostPortListener = std::function<devices::cable::PlugOrSocket(devices::cable::SocketPtr)>;

/**
 * @brief A registry for managing and listening for connections to virtual devices.
 *
 * The `ConnectorRegistry` class facilitates the registration and connection of
 * virtual devices, primarily within the QEMU environment. It allows QEMU devices
 * to register themselves during the initial registration phase. Subsequently,
 * during the QEMU launch phase, the `listen` call can be made to activate these
 * registered devices and make them accessible to the guest system.
 *
 * **Important:** Each device type is unique within the registry. This means
 * that only one device of a given type (e.g., SensorDevice, GPSDevice) can be
 * registered at any time. If a device of the same type is registered again,
 * it will replace the previously registered device. Devices are typically
 * registered early in the boot process, but they can also be registered again
 * after a reboot.
 *
 * @see goldfish/devices/Connector.h for details on the underlying protocol.
 */
class ConnectorRegistry : public IConnectorRegistry {
  public:
    /**
     * @brief Function signature for starting a listener.
     *
     * @param listener The `HostPortListener` to be used for accepting connections.
     * @return `true` if the listener was started successfully, `false` otherwise.
     */
    using ListenFn = std::function<bool(HostPortListener)>;

    ConnectorRegistry();
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

    bool registerQemuDevice(std::string_view name, DeviceFactory factory) override;

    bool registerDevice(std::string_view name, DeviceFactory factory) override;

    void registerHalDevice(std::string name, async::EventLoop* clientLoop,
                           async::EventLoop* qemuLoop, HalDeviceFactory factory) override;

    void registerHalQemuDevice(std::string name, async::EventLoop* clientLoop,
                               async::EventLoop* qemuLoop, HalDeviceFactory factory) override;

    static ConnectorRegistry& defaultRegistry();

  private:
    bool registerDeviceImpl(std::string_view prefix, std::string_view name, DeviceFactory factory);

    using DeviceRegistration = std::function<bool(std::string, DeviceFactory)>;
    void registerHalDeviceImpl(std::string name, async::EventLoop* clientLoop,
                               async::EventLoop* qemuLoop, HalDeviceFactory factory,
                               DeviceRegistration registerFn);

    const std::shared_ptr<PingTopic> mPingTopic;
    bool mAcceptingRegistries = true;
    std::mutex mEntriesMutex;
    absl::flat_hash_map<std::string, DeviceFactory> mEntries;
    std::vector<DeviceEntry> mDevices;
};

}  // namespace devices
}  // namespace goldfish
