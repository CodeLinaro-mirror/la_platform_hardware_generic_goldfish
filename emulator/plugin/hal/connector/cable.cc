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

#include "goldfish/devices/cable/cable.h"

#include <unordered_map>

#include "goldfish/devices/cable/saveload.h"

namespace goldfish::devices::cable {
namespace {
using PlugLoadersMap = std::unordered_map<IPlug::TypeId, PlugLoader>;

/* We can't rely on the initialization order of global variables.
 * See Meyers' Singleton.
 */
PlugLoadersMap& GetLoaders() {
    static PlugLoadersMap instance;
    return instance;
}
}  // namespace

void ISocket::SetOnFlowControlEvent(ISocket::OnFlowControlEvent) {  // NOLINT
    // Do nothing. Or maybe log that flow control is not supported here.
}

bool RegisterPlugLoader(IPlug::TypeId type_id, PlugLoader loader) {
    return GetLoaders().insert({std::move(type_id), std::move(loader)}).second;
}

using archive::IWriter;

bool SavePlugToSnapshot(const IPlug& plug, IWriter& writer) {
    if (!plug.SupportsLoadingFromSnapshot()) {
        return false;
    }

    const std::string id = plug.GetSnapshotTypeId();
    if (id.empty()) {
        return false;
    }

    writer << id;
    return plug.SaveStateToSnapshot(writer);
}

using archive::IReader;

// NOLINTNEXTLINE
PlugOrSocket LoadPlugFromSnapshot(SocketPtr socket, IReader& reader) {
    const auto id = ReadValue<std::string>(reader);
    if (!id.ok()) {
        return socket;
    }

    const auto& loaders = GetLoaders();
    const auto i = loaders.find(*id);
    if (i == loaders.end()) {
        return socket;
    }

    return (i->second)(std::move(socket), reader);
}
}  // namespace goldfish::devices::cable
