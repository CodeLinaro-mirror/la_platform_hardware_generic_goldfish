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
#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace vsock {

devices::cable::SocketPtr connect(uint32_t guestPort, devices::cable::PlugPtr);

/**
 * @brief Connects to a guest port, wrapping the connection with a
 * MarshallingPlug.
 *
 * The provided `clientPlug` will be called on the `clientLoop`.
 *
 * @param guestPort The guest port to connect to.
 * @param clientPlug The plug to connect to the guest.
 * @param clientLoop The event loop on which the plug will be called.
 * @return A socket pointer to the marshalling plug.
 */
devices::cable::SocketPtr connectWithMarshalling(uint32_t guestPort,
                                                 devices::cable::PlugPtr clientPlug,
                                                 async::EventLoop* clientLoop);
}  // namespace vsock
}  // namespace goldfish
