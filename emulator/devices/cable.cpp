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

#include <unordered_map>
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/cable/saveload.h"
#include "goldfish/QEMUFile.h"

namespace goldfish {
namespace devices {
namespace cable {
namespace {
using PlugLoadersMap = std::unordered_map<IPlug::TypeId, PlugLoader>;

/* We can't rely on the initialization order of global variables.
 * See Meyers' Singleton.
 */
PlugLoadersMap &getLoaders() {
    static PlugLoadersMap instance;
    return instance;
}
}  // namespace

bool registerPlugLoader(IPlug::TypeId typeId, PlugLoader loader) {
    return getLoaders().insert({std::move(typeId),
                                std::move(loader)}).second;
}

bool savePlugToSnapshot(const IPlug &plug, QEMUFile *const file) {
    if (!plug.supportsLoadingFromSnapshot()) {
        return false;
    }

    const std::string id = plug.getSnapshotTypeId();
    const size_t idSize = id.size();
    if ((idSize == 0) || (idSize > UINT8_MAX)) {
        return false;
    }

    qemu_put_byte(file, idSize);
    qemu_put_buffer(file, reinterpret_cast<const uint8_t *>(id.data()), idSize);
    return plug.saveStateToSnapshot(file);
}

PlugOrSocket loadPlugFromSnapshot(SocketPtr socket, QEMUFile *const file) {
    const size_t idSize = qemu_get_byte(file);
    if (idSize == 0) {
        return socket;
    }

    std::string id(idSize, '?');
    if (qemu_get_buffer(file, reinterpret_cast<uint8_t *>(id.data()), idSize) != idSize) {
        return socket;
    }

    const auto &loaders = getLoaders();
    const auto i = loaders.find(id);
    if (i == loaders.end()) {
        return socket;
    }

    return (i->second)(std::move(socket), file);
}

}  // namespace cable
}  // namespace devices
}  // namespace goldfish
