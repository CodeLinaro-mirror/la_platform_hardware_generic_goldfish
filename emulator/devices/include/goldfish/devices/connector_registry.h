
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

#include <goldfish/devices/cable/cable.h>

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

using HostPortListener = std::function<devices::cable::PlugOrSocket(devices::cable::SocketPtr)>;

struct IConnectorRegistry {
    virtual ~IConnectorRegistry() = default;

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
    virtual bool registerQemuDevice(std::string name, Connector::DeviceFactory factory) = 0;

    /**
     * @brief Registers a device with the registry.
     *
     * @param name The name of the device.
     * @param factory The factory function for creating the device.
     * @return `true` if the device was registered successfully, `false` otherwise.
     */
    virtual bool registerDevice(std::string name, Connector::DeviceFactory factory) = 0;
};

/**
 * @brief A no-op implementation of the IConnectorRegistry interface.
 *
 * This class provides a null or no-op implementation of the `IConnectorRegistry`
 * interface.  It's primarily useful for testing or in situations where a
 * `ConnectorRegistry` is required but no actual device registration is needed.
 * All registration methods simply return `true`, effectively ignoring any
 * registration requests.
 */
class NullConnectorRegistry : public IConnectorRegistry {
    bool registerQemuDevice(std::string name, Connector::DeviceFactory factory) override {
        return true;
    }

    bool registerDevice(std::string name, Connector::DeviceFactory factory) override {
        return true;
    };
};

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
class ConnectorRegistry : public IConnectorRegistry {
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

    bool registerQemuDevice(std::string name, Connector::DeviceFactory factory) override;

    bool registerDevice(std::string name, Connector::DeviceFactory factory) override;

    static ConnectorRegistry& defaultRegistry();

    /**
     * @brief Retrieves a weak pointer to an active device, if any.
     *
     * An active device is a device that has been created in response to a
     * guest request. The guest requests a device by name using the `pipe:`
     * protocol over virtio-vsock. If the device has been unplugged by the
     * guest (via a call to `IPlug::onUnplug`), the `weak_ptr` will be empty
     * and the entry removed from the internal active device map.
     *
     * @tparam T The expected type of the device.
     * @param name The name of the device.
     * @return A `weak_ptr` to the active device, or an empty `weak_ptr` if
     *         no device with the given name is currently active.
     */
    template <typename T>
    std::weak_ptr<T> activeDevice(std::string name) {
        std::lock_guard<std::mutex> lock(mActivePlugsMutex);
        auto it = mActivePlugs.find(name);
        if (it != mActivePlugs.end()) {
            if (auto plugPtr = it->second.lock()) {
                // Try to cast the shared_ptr to the target type
                if (auto castPtr = std::dynamic_pointer_cast<T>(plugPtr)) {
                    return std::weak_ptr<T>(castPtr);
                }
                // If cast fails, return empty weak_ptr
                return std::weak_ptr<T>();
            } else {
                // The device has been unplugged; remove the stale entry.
                mActivePlugs.erase(it);
            }
        }
        return std::weak_ptr<T>();
    }

  private:
    std::shared_ptr<PingTopic> mPingTopic;
    bool mAcceptingRegistries;
    std::mutex mEntriesMutex;
    std::mutex mActivePlugsMutex;
    absl::flat_hash_map<std::string, Connector::DeviceFactory> mEntries;
    absl::flat_hash_map<std::string, std::weak_ptr<cable::IPlug>> mActivePlugs;
    std::vector<Connector::DeviceEntry> mDevices;
};
}  // namespace devices
}  // namespace goldfish
