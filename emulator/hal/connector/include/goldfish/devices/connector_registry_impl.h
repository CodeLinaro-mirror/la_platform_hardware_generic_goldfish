
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
#include "absl/log/log.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/device_entry.h"
#include "goldfish/hal/plug/HalPlugToIPlugAdapter.h"

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
     * @return A `weak_ptr` to the active device, or an empty `weak_ptr` if
     *         no device with the given name is currently active.
     */
    template <typename T>
    std::weak_ptr<T> activeDevice() {
        std::lock_guard<std::mutex> lock(mActivePlugsMutex);
        auto it = mActivePlugs.find(T::serviceName);
        if (it == mActivePlugs.end()) {
            return {};
        }

        const std::shared_ptr<cable::IPlug> impl = it->second.lock();
        if (!impl) {
            mActivePlugs.erase(it);
            return {};
        }

        std::shared_ptr<T> result;
        if constexpr (std::derived_from<T, cable::IPlug>) {
            result = std::dynamic_pointer_cast<T>(impl);
        } else if constexpr (std::derived_from<T, HalPlug>) {
            if (auto adapter = std::dynamic_pointer_cast<HalPlugToIPlugAdapter>(impl)) {
                result = std::dynamic_pointer_cast<T>(adapter->getHalPlug());
            }
        } else {
            static_assert(false, "We should not get here");
        }

        if (!result) {
            LOG(DFATAL) << "The '" << T::serviceName
                        << "' service was found but it was registered with an incompatible type.";
        }

        return std::weak_ptr<T>(std::move(result));
    }

  protected:
    /**
     * @brief Registers an active device internally.
     *
     * This method is called when a new device is created and connected. It stores
     * a weak pointer to the device's plug in the internal active device map and
     * fires an event to notify any listeners that a new device has been registered.
     *
     * @param registryName The name under which the device was registered.
     * @param plug A weak pointer to the device's plug.
     *
     * @protected This method is protected to allow access from test classes.
     */
    void registerInternal(std::string registryName, const std::shared_ptr<cable::IPlug>& plug);

  private:
    bool registerDeviceImpl(std::string_view prefix, std::string_view name, DeviceFactory factory);

    using DeviceRegistration = std::function<bool(std::string, DeviceFactory)>;
    void registerHalDeviceImpl(std::string name, async::EventLoop* clientLoop,
                               async::EventLoop* qemuLoop, HalDeviceFactory factory,
                               DeviceRegistration registerFn);

    const std::shared_ptr<PingTopic> mPingTopic;
    bool mAcceptingRegistries = true;
    std::mutex mEntriesMutex;
    std::mutex mActivePlugsMutex;
    absl::flat_hash_map<std::string, DeviceFactory> mEntries;

    // This map holds weak pointers to all currently active plugs, allowing for
    // inspection via the `activeDevice<T>()` method.
    absl::flat_hash_map<std::string, std::weak_ptr<cable::IPlug>> mActivePlugs;
    std::vector<DeviceEntry> mDevices;
};

template <class IDevice>
using DeviceRegistrationCallbacks =
        android::base::eventing::CallbackEventSource<std::weak_ptr<IDevice>>;

/**
 * @brief Listens for the registration of a specific device type within the ConnectorRegistry.
 *
 * This class allows you to register callbacks or listeners that are invoked when a new device
 * of a specific type is registered with the `ConnectorRegistry`.
 *
 * **Event Firing:**
 * - Upon the successful registration of a device matching the specified type, the
 *   `DeviceRegistrationListener` will trigger all of its registered callbacks, providing them
 *   with a `std::weak_ptr` to the newly created device.
 * - If a device of the specified type is already active when a new callback or listener is
 *   added, an event will be fired immediately for that callback or listener.
 * - It is possible for a callback or listener to receive the same event twice: once when it is
 *   added if the device is already present, and again when the device is created if it is not
 *   already present.
 *
 * This `weak_ptr` allows you to safely access the device if it is still alive. If the device is
 * destroyed before the `weak_ptr` is locked, then `weak.lock()` will return null.
 *
 * **Key Use Cases:**
 *
 * - **Device Availability Notification:** Be notified when a particular device, such as a
 *   sensor or a virtual hardware component, becomes available in the guest.
 * - **Dynamic Device Handling:** Respond dynamically to the availability of devices,
 *   allowing your code to adapt to changes in the emulator's configuration.
 * - **Delayed Device Access:** Obtain a `weak_ptr` to a device, allowing you to access it
 *   safely at a later time, even if its lifetime is managed elsewhere.
 *
 * **Example Usage:**
 *
 * The following example demonstrates how to use `DeviceRegistrationListener` to be notified
 * when an `ISensorDevice` becomes available and then read the accelerometer sensor data:
 *
 * ```cpp
 * ConnectorRegistry* registry;
 * DeviceRegistrationListener<ISensorDevice> listener(registry);
 *
 * listener.addCallback([&](std::weak_ptr<ISensorDevice> weak) {
 *   if (auto sensor = weak.lock()) {
 *     auto statusOrData = sensor->getSensorData(AndroidSensor::ANDROID_SENSOR_ACCELEROMETER);
 *     // Do something with the data..
 *   }
 * });
 * ```
 *
 * **Template Parameter:**
 *
 * @tparam IDevice The type of device to listen for. This type must meet the following requirements:
 *   - It must have a static member called `serviceName` of type `std::string_view` or a type that
 *     is convertible to  `std::string_view`.
 *   - `serviceName` holds the name under which the device was registered in the
 *     `ConnectorRegistry`.
 */
template <class IDevice>
class DeviceRegistrationListener : public DeviceRegistrationCallbacks<IDevice> {
  public:
    using RegistrationCallbackId = DeviceRegistrationCallbacks<IDevice>::CallbackId;
    using RegistrationEventCallback = DeviceRegistrationCallbacks<IDevice>::EventCallback;

    DeviceRegistrationListener(ConnectorRegistry* registry) : mRegistry(registry) {
        mCallbackId = registry->addCallback([this](const std::string& name) {
            if (name == IDevice::serviceName) {
                DeviceRegistrationListener::fireEvent(mRegistry->activeDevice<IDevice>());
            }
        });
    }

    RegistrationCallbackId addCallback(RegistrationEventCallback callback) override {
        auto id = DeviceRegistrationCallbacks<IDevice>::addCallback(callback);
        // If a device becomes alive right now, we will see 2 events..
        auto device = mRegistry->activeDevice<IDevice>();
        // Only fire an event if the device is not expired (i.e. it exists).
        if (!device.expired()) {
            DeviceRegistrationListener::fireEvent(device);
        }
        return id;
    }

    ~DeviceRegistrationListener() { mRegistry->removeCallback(mCallbackId); }

  private:
    ConnectorRegistry::CallbackId mCallbackId;
    ConnectorRegistry* mRegistry;
};

}  // namespace devices
}  // namespace goldfish
