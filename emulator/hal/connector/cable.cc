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

namespace goldfish {
namespace devices {
namespace cable {
namespace {
using PlugLoadersMap = std::unordered_map<IPlug::TypeId, PlugLoader>;

/* We can't rely on the initialization order of global variables.
 * See Meyers' Singleton.
 */
PlugLoadersMap& getLoaders() {
    static PlugLoadersMap instance;
    return instance;
}
}  // namespace

void ISocket::setOnFlowControlEvent(ISocket::OnFlowControlEvent) {
    // Do nothing. Or maybe log that flow control is not supported here.
}

bool registerPlugLoader(IPlug::TypeId typeId, PlugLoader loader) {
    return getLoaders().insert({std::move(typeId), std::move(loader)}).second;
}

using archive::IWriter;

bool savePlugToSnapshot(const IPlug& plug, IWriter& writer) {
    if (!plug.supportsLoadingFromSnapshot()) {
        return false;
    }

    const std::string id = plug.getSnapshotTypeId();
    if (id.empty()) {
        return false;
    }

    writer << id;
    return plug.saveStateToSnapshot(writer);
}

using archive::IReader;

PlugOrSocket loadPlugFromSnapshot(SocketPtr socket, IReader& reader) {
    const std::string id = GetString(reader);
    if (id.empty()) {
        return socket;
    }

    const auto& loaders = getLoaders();
    const auto i = loaders.find(id);
    if (i == loaders.end()) {
        return socket;
    }

    return (i->second)(std::move(socket), reader);
}

}  // namespace cable
}  // namespace devices
}  // namespace goldfish
