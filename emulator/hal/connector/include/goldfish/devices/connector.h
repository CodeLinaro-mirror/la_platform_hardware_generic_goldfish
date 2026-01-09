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
 */

struct Connector : public cable::IPlug {
    Connector(cable::SocketPtr socket, std::shared_ptr<PingTopic> ping_topic,
              const DeviceEntry* devices_entries, size_t devices_entries_size);

    cable::SocketPtr OnUnplug() override;
    bool OnReceive(const void* data, size_t size) override;

    bool SupportsLoadingFromSnapshot() const override;
    TypeId GetSnapshotTypeId() const override;
    bool SaveStateToSnapshot(archive::IWriter&) const override;

  private:
    using Buffer = std::vector<char>;

    std::pair<bool, cable::PlugPtr> ProcessRequest(std::string_view request, const void* unconsumed,
                                                   size_t unconsumed_size, const Buffer& buffer);
    std::pair<bool, cable::PlugPtr> SwitchTo(bool is_qemud, std::string_view device,
                                             std::string_view args, const void* unconsumed,
                                             size_t unconsumed_size);

    cable::SocketPtr socket_;
    const std::shared_ptr<PingTopic> ping_topic_;
    const DeviceEntry* const devices_entries_;
    const size_t devices_entries_size_;
    Buffer buffer_;
};

}  // namespace goldfish::devices
