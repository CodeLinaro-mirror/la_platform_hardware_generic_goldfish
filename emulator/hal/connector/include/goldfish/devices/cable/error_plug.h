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

namespace goldfish::devices::cable {

struct ErrorPlug : public IPlug {
    explicit ErrorPlug(cable::SocketPtr socket) : socket(std::move(socket)) {}

    cable::SocketPtr OnUnplug() override { return std::move(socket); }

    bool OnReceive(const void*, size_t) override { return false; }

    cable::SocketPtr socket;
};

}  // namespace goldfish::devices::cable
