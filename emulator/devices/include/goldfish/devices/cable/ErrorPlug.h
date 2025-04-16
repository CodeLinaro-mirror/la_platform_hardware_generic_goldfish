/* Copyright (C) 2025 The Android Open Source Project
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
#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace devices {
namespace cable {

struct ErrorPlug : public IPlug {
    ErrorPlug(cable::SocketPtr socket) : mSocket(std::move(socket)) {}

    cable::SocketPtr onUnplug() override { return std::move(mSocket); }

    bool onReceive(const void*, size_t) override { return false; }

    cable::SocketPtr mSocket;
};

}  // namespace cable
}  // namespace devices
}  // namespace goldfish
