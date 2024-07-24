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
#include <string_view>
#include <vector>

#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/PingTopic.h"

namespace goldfish {
namespace devices {

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
    using DeviceFactory = std::function<cable::PlugPtr(
        cable::SocketPtr socket, PingTopic &pingTopic, std::string_view args)>;

    struct DeviceEntry {
        const char *qname;  // prefixed with 'q' for qemud, use '-' otherwise
        DeviceFactory factory;
    };

    Connector(cable::SocketPtr socket, PingTopic &pingTopic,
              const DeviceEntry *devicesEntries, size_t devicesEntriesSize);

    cable::SocketPtr onUnplug() override;
    bool onReceive(const void *data, size_t size) override;

    bool supportsLoadingFromSnapshot() const override;
    TypeId getSnapshotTypeId() const override;
    bool saveStateToSnapshot(archive::IWriter &) const override;

private:
    using Buffer = std::vector<char>;

    bool processRequest(std::string_view request, const void *unconsumed,
                        size_t unconsumedSize, Buffer buffer);
    bool switchTo(bool isQemud, std::string_view device, std::string_view args,
                  const void *unconsumed, size_t unconsumedSize);

    cable::SocketPtr mSocket;
    PingTopic &mPingTopic;
    const DeviceEntry *const mDevicesEntries;
    const size_t mDevicesEntriesSize;
    Buffer mBuffer;
};

}  // namespace devices
}  // namespace goldfish
