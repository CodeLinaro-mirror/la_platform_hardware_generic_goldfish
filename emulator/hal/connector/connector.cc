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

#include "goldfish/devices/connector.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "goldfish/devices/cable/error_plug.h"

namespace goldfish {
namespace devices {
using cable::ErrorPlug;
using cable::PlugPtr;
using cable::SocketPtr;

namespace {
bool qnameEquals(const char q, const std::string_view name, const std::string_view qname) {
    return (qname.size() > 1) && (q == qname[0]) && (name == qname.substr(1));
}
}  // namespace

Connector::Connector(SocketPtr socket, std::shared_ptr<PingTopic> pingTopic,
                     const DeviceEntry* devicesEntries, const size_t devicesEntriesSize)
        : mSocket(std::move(socket))
        , mPingTopic(std::move(pingTopic))
        , mDevicesEntries(devicesEntries)
        , mDevicesEntriesSize(devicesEntriesSize) {}

SocketPtr Connector::OnUnplug() {
    return std::move(mSocket);
}

bool Connector::OnReceive(const void* data, const size_t size) {
    PlugPtr self;  // to keep `this` alive until exit from this function

    const char* const data8 = static_cast<const char*>(data);
    const char* const end8 = data8 + size;

    // Find null terminator (end of request) in the incoming data
    const auto zero8 = std::find(data8, end8, 0);
    if (zero8 != end8) {
        bool result;
        if (mBuffer.empty()) {
            std::string_view request(data8, zero8 - data8);
            std::tie(result, self) = processRequest(request, zero8 + 1, end8 - (zero8 + 1), {});
        } else {
            mBuffer.insert(mBuffer.end(), data8, end8);
            const size_t requestSize = mBuffer.size() - (end8 - zero8);

            std::string_view request(mBuffer.data(), requestSize);
            std::tie(result, self) =
                    processRequest(request, &mBuffer[requestSize + 1],
                                   mBuffer.size() - (requestSize + 1), std::move(mBuffer));
        }

        if (!result) {
            auto& socket = *mSocket;
            self = socket.SwitchPlug(std::make_shared<ErrorPlug>(std::move(mSocket)));
            // ~Connector is called here
        }
        return result;
    } else {
        // Append data to the internal buffer (up to null terminator or full data).
        // Please note that requests are allowed to arrive in parts.
        mBuffer.insert(mBuffer.end(), data8, end8);
        return true;
    }
}

std::pair<bool, PlugPtr> Connector::processRequest(
        std::string_view request, const void* const unconsumed, const size_t unconsumedSize,
        const Buffer bufferPassedHereForLifetimePurposes) {
    using namespace std::literals;

    constexpr auto kPipePrefix = "pipe:"sv;
    if (request.starts_with(kPipePrefix)) {
        request.remove_prefix(kPipePrefix.size());
    } else {
        return {false, {}};
    }

    constexpr auto kQemudPrefix = "qemud:"sv;
    bool isQemud;
    if (request.starts_with(kQemudPrefix)) {
        request.remove_prefix(kQemudPrefix.size());
        isQemud = true;
    } else {
        isQemud = false;
    }

    std::string_view device;
    std::string_view args;

    const size_t colon = request.find(':');
    if (colon != std::string_view::npos) {
        device = request.substr(0, colon);
        args = request.substr(colon + 1, request.size() - colon - 1);
    } else {
        device = request;
    }

    if (device.empty()) {
        return {false, {}};
    } else {
        return switchTo(isQemud, device, args, unconsumed, unconsumedSize);
    }
}

std::pair<bool, PlugPtr> Connector::switchTo(const bool isQemud, const std::string_view device,
                                             const std::string_view args,
                                             const void* const unconsumed,
                                             const size_t unconsumedSize) {
    const char q = isQemud ? 'q' : '-';
    size_t n = mDevicesEntriesSize;
    for (const DeviceEntry* de = mDevicesEntries; n > 0; ++de, --n) {
        if (qnameEquals(q, device, de->qname)) {
            auto& socket = *mSocket;
            PlugPtr newPlug = de->factory(std::move(mSocket), mPingTopic, args);
            auto& newPlugRef = *newPlug;
            PlugPtr self = socket.SwitchPlug(std::move(newPlug));
            newPlugRef.OnReceive(unconsumed, unconsumedSize);
            return {true, std::move(self)};
        }
    }

    return {false, {}};
}

bool Connector::SupportsLoadingFromSnapshot() const {
    return true;
}

cable::IPlug::TypeId Connector::GetSnapshotTypeId() const {
    using namespace std::string_literals;
    return "Connector"s;
}

bool Connector::SaveStateToSnapshot(archive::IWriter& writer) const {
    writer << mBuffer.size();
    writer.Write(mBuffer.data(), mBuffer.size());
    return true;
}

}  // namespace devices
}  // namespace goldfish
