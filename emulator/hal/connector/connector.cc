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

namespace goldfish::devices {
using cable::ErrorPlug;
using cable::PlugPtr;
using cable::SocketPtr;

namespace {
bool QnameEquals(const char q, const std::string_view name, const std::string_view qname) {
    return (qname.size() > 1) && (q == qname[0]) && (name == qname.substr(1));
}
}  // namespace

Connector::Connector(SocketPtr socket, std::shared_ptr<PingTopic> ping_topic,
                     const DeviceEntry* devices_entries, const size_t devices_entries_size)
        : socket_(std::move(socket))
        , ping_topic_(std::move(ping_topic))
        , devices_entries_(devices_entries)
        , devices_entries_size_(devices_entries_size) {}

SocketPtr Connector::OnUnplug() {
    return std::move(socket_);
}

bool Connector::OnReceive(const void* data, const size_t size) {
    PlugPtr self;  // to keep `this` alive until exit from this function

    const char* const data8 = static_cast<const char*>(data);
    const char* const end8 = data8 + size;

    // Find null terminator (end of request) in the incoming data
    const auto* const zero8 = std::find(data8, end8, 0);
    if (zero8 != end8) {
        bool result;
        if (buffer_.empty()) {
            const std::string_view request(data8, zero8 - data8);
            std::tie(result, self) = ProcessRequest(request, zero8 + 1, end8 - (zero8 + 1), {});
        } else {
            buffer_.insert(buffer_.end(), data8, end8);
            const size_t request_size = buffer_.size() - (end8 - zero8);

            const std::string_view request(buffer_.data(), request_size);
            std::tie(result, self) = ProcessRequest(request, &buffer_[request_size + 1],
                                                    buffer_.size() - (request_size + 1), buffer_);
        }

        if (!result) {
            auto& socket = *socket_;
            self = socket.SwitchPlug(std::make_shared<ErrorPlug>(std::move(socket_)));
            // ~Connector is called here
        }
        return result;
    }  // Append data to the internal buffer (up to null terminator or full data).
    // Please note that requests are allowed to arrive in parts.
    buffer_.insert(buffer_.end(), data8, end8);
    return true;
}

std::pair<bool, PlugPtr> Connector::ProcessRequest(std::string_view request,
                                                   const void* const unconsumed,
                                                   const size_t unconsumed_size,
                                                   const Buffer& /*lifetime_assurance*/) {
    using namespace std::literals;

    constexpr auto kPipePrefix = "pipe:"sv;
    if (request.starts_with(kPipePrefix)) {
        request.remove_prefix(kPipePrefix.size());
    } else {
        return {false, {}};
    }

    constexpr auto kQemudPrefix = "qemud:"sv;
    bool is_qemud;
    if (request.starts_with(kQemudPrefix)) {
        request.remove_prefix(kQemudPrefix.size());
        is_qemud = true;
    } else {
        is_qemud = false;
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
    }
    return SwitchTo(is_qemud, device, args, unconsumed, unconsumed_size);
}

std::pair<bool, PlugPtr> Connector::SwitchTo(const bool is_qemud, const std::string_view device,
                                             const std::string_view args,
                                             const void* const unconsumed,
                                             const size_t unconsumed_size) {
    const char q = is_qemud ? 'q' : '-';
    size_t n = devices_entries_size_;
    for (const DeviceEntry* de = devices_entries_; n > 0; ++de, --n) {
        if (QnameEquals(q, device, de->qname)) {
            auto& socket = *socket_;
            PlugPtr new_plug = de->factory(std::move(socket_), ping_topic_, args);
            auto& new_plug_ref = *new_plug;
            PlugPtr self = socket.SwitchPlug(std::move(new_plug));
            new_plug_ref.OnReceive(unconsumed, unconsumed_size);
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
    writer << buffer_.size();
    writer.Write(buffer_.data(), buffer_.size());
    return true;
}

}  // namespace goldfish::devices
