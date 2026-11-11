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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/connector.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/device_entry.h"

namespace goldfish::devices {

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
 * **Snapshots:** `Listen` publishes this registry under a snapshot id
 * (`"vsock:<port>"` for `Listen(int)`, the empty id for `Listen(ListenFn)`)
 * and installs the process-wide `Connector` plug loader
 * (`cable::RegisterPlugLoader` keys loaders by type id and has no
 * unregister). A guest connection captured before it finished sending its
 * pipe name is saved with that id and restored as a `Connector` over the
 * device table of the registry currently listening under it; the destructor
 * withdraws the id again.
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
    explicit ConnectorRegistry(std::shared_ptr<PingTopic> ping_topic);
    ~ConnectorRegistry() override;

    /**
     * @brief Starts listening for connections on the specified vsock port.
     *
     * This creates a vsock server socket on the host side, allowing the guest
     * to connect to this port and access the registered virtual devices.
     *
     * @param port The vsock port number to listen on.
     * @return `true` if the listener was started successfully, `false` otherwise.
     */
    bool Listen(int port);

    /**
     * @brief Starts listening for connections using a custom listen function.
     *
     * This method is mainly used for testing.
     *
     * @param startListening The function to be used for starting the listener.
     * @return `true` if the listener was started successfully, `false` otherwise.
     */
    bool Listen(const ListenFn& start_listening);

    bool RegisterQemuDevice(std::string_view name, DeviceFactory factory) override;

    bool RegisterDevice(std::string_view name, DeviceFactory factory) override;

    void RegisterHalDevice(
            std::string name, async::EventLoop* client_loop, async::EventLoop* qemu_loop,
            HalDeviceFactory factory) override;  // NOLINT(performance-unnecessary-value-param)

    void RegisterHalQemuDevice(
            std::string name, async::EventLoop* client_loop, async::EventLoop* qemu_loop,
            HalDeviceFactory factory) override;  // NOLINT(performance-unnecessary-value-param)

    /**
     * @brief Creates a `Connector` bound to `socket` for the registry published
     * under `snapshot_id` (e.g. `"vsock:5000"`), or returns `nullptr` if no
     * registry is published under that id.
     */
    static cable::PlugPtr CreateConnector(cable::SocketPtr socket, std::string_view snapshot_id);

  private:
    bool RegisterDeviceImpl(std::string_view prefix, std::string_view name, DeviceFactory factory);

    bool ListenImpl(std::string snapshot_id, const ListenFn& start_listening);

    // The `cable::PlugLoader` for `Connector::kSnapshotTypeId`; resolves the
    // saved registry id through the process-wide table `Listen` fills.
    static cable::PlugOrSocket LoadConnectorFromSnapshot(cable::SocketPtr socket,
                                                         archive::IReader& reader);
    static void EnsureConnectorLoaderRegistered();
    static std::optional<Connector::Target> FindSnapshotTarget(std::string_view snapshot_id);
    void PublishSnapshotTarget();
    void WithdrawSnapshotTarget();

    using DeviceRegistration = std::function<bool(std::string, DeviceFactory)>;
    // `prefix` is "q" for qemud devices and "-" otherwise (see DeviceEntry::qname).
    void RegisterHalDeviceImpl(
            std::string name, std::string_view prefix, async::EventLoop* client_loop,
            async::EventLoop* qemu_loop, HalDeviceFactory factory,
            const DeviceRegistration& register_fn);  // NOLINT(performance-unnecessary-value-param)

    const std::shared_ptr<PingTopic> ping_topic_;
    bool accepting_registries_ = true;
    bool snapshot_target_published_ = false;
    std::mutex entries_mutex_;
    absl::flat_hash_map<std::string, DeviceFactory> entries_;
    std::vector<DeviceEntry> devices_;
    // Key under which `Listen` published this registry for Connector
    // snapshots; see `PublishSnapshotTarget`.
    std::string snapshot_id_;
    // HalPlug devices this registry added to HalPlugSnapshotRegistry; removed
    // again on destruction.
    std::vector<std::string> snapshot_devices_;
};

}  // namespace goldfish::devices
