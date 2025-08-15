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

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace vsock {

using HostPortListener = std::function<devices::cable::PlugOrSocket(devices::cable::SocketPtr)>;

bool listen(uint32_t hostPort, HostPortListener);

/**
 * @brief Listens on a host port and wraps incoming connections with a
 * MarshallingPlug.
 *
 * The provided listener will be called on the `clientLoop` with the
 * marshalling socket. The listener should return a plug to be connected to the
 * other side of the marshaller.
 *
 * @param hostPort The host port to listen on.
 * @param listener The listener to be called on a new connection.
 * @param clientLoop The event loop on which the listener will be called.
 * @return true if listening started successfully, false otherwise.
 */
bool listenWithMarshalling(uint32_t hostPort, vsock::HostPortListener listener,
                           async::EventLoop* clientLoop);
}  // namespace vsock
}  // namespace goldfish
