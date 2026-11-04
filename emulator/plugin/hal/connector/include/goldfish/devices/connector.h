/* Copyright (C) 2024 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/device_entry.h"
#include "goldfish/devices/ping_topic.h"

namespace goldfish::devices {

/*
 * Connector is a cable::IPlug that allows to switch to a different devices
 * by sending a special message to the socket:
 *
 * pipe:<device>:<args>Z
 *
 * or
 *
 * pipe:qemud:<device>:<args>Z
 *
 * where
 * * 'pipe:' - fixed prefix;
 * * 'qemud:' - a historic protocol artefact, optional;
 * * <device> - is the device name of the device to switch to;
 * * ':' - separator
 * * <args> - is an optional device specific arguments string.
 * * <Z> - is a zero byte (request terminator);
 *
 * The device name is matched against the `qname` field in the `DeviceEntry`
 * structure passed in the `Connector` constructor. `qname` is prefixed
 * with 'q' for "qemud:" devices and with '-' otherwise.
 *
 * The device is created by calling the factory function, see
 * `Connector::DeviceFactory`.
 *
 * The device is then connected to the socket and the Connector is replaced
 * with the device. All the extra data that `Connector` received is passed to
 * next device. The `Connecor` instance is destroyed once switching is done.
 *
 * If the `Connector` cannot process a request (a parsing error or the device
 * name is not found in the list provided) it switches to `ErrorPlug` which
 * asks to disconnect.
 *
 * Snapshots: a `Connector` is saved as `kSnapshotTypeId`, the id of the
 * registry it belongs to and the (possibly empty) partial request received
 * so far, so a guest connection that was captured before it finished sending
 * its pipe name resumes exactly where it stopped. `LoadFromSnapshot` is the
 * matching loader; the `ConnectorRegistry` registers it with
 * `cable::RegisterPlugLoader` and resolves the registry id back to a device
 * table.
 */

struct Connector : public cable::IPlug {
    static constexpr std::string_view kSnapshotTypeId = "Connector";

    /// Upper bound for the partial request restored from a snapshot. Pipe
    /// names are a few dozen bytes; anything larger is a corrupt snapshot.
    static constexpr size_t kMaxSnapshotBufferSize = 4096;

    /// Where a restored Connector switches devices: the device table of the
    /// registry identified by the snapshot's registry id.
    struct Target {
        std::shared_ptr<PingTopic> ping_topic;
        const DeviceEntry* devices_entries = nullptr;
        size_t devices_entries_size = 0;
    };
    using TargetResolver = std::function<std::optional<Target>(std::string_view registry_id)>;

    /**
     * @param registry_id Opaque id saved with the snapshot and handed to the
     * `TargetResolver` on load (e.g. "vsock:5000"); several registries can
     * listen at once and each restored Connector must find its own.
     */
    Connector(cable::SocketPtr socket, std::shared_ptr<PingTopic> ping_topic,
              const DeviceEntry* devices_entries, size_t devices_entries_size,
              std::string registry_id = {});

    /**
     * @brief Re-creates a `Connector` saved by `SaveStateToSnapshot`.
     *
     * Reads the registry id and partial request from `reader` (the type id
     * has already been consumed by `cable::LoadPlugFromSnapshot`). Returns
     * `socket` back if the record is unreadable, too large, contains a
     * request terminator, or `resolve` does not know the registry id.
     */
    static cable::PlugOrSocket LoadFromSnapshot(cable::SocketPtr socket, archive::IReader& reader,
                                                const TargetResolver& resolve);

    cable::SocketPtr OnUnplug() override;
    bool OnReceive(const void* data, size_t size) override;

    bool SupportsLoadingFromSnapshot() const override;
    TypeId GetSnapshotTypeId() const override;
    bool SaveStateToSnapshot(archive::IWriter&) const override;

  private:
    using Buffer = std::vector<char>;

    Connector(cable::SocketPtr socket, Target target, std::string registry_id, Buffer buffer);

    std::pair<bool, cable::PlugPtr> ProcessRequest(std::string_view request, const void* unconsumed,
                                                   size_t unconsumed_size, const Buffer& buffer);
    std::pair<bool, cable::PlugPtr> SwitchTo(bool is_qemud, std::string_view device,
                                             std::string_view args, const void* unconsumed,
                                             size_t unconsumed_size);

    cable::SocketPtr socket_;
    const std::shared_ptr<PingTopic> ping_topic_;
    const DeviceEntry* const devices_entries_;
    const size_t devices_entries_size_;
    const std::string registry_id_;
    Buffer buffer_;
};

}  // namespace goldfish::devices
