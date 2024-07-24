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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace devices {
namespace qemud {

/* The QEMUD packet: [size][payload]
 *
 * size (4 bytes):
 *   ASCII encoding: 4 hex characters
 *   binary encoding: 31bit big-endian value with the 31st bit set
 * payload: device specific
 *
 * `Parser` accepts the incoming stream of bytes (delivered to `onReceive`)
 * and calls the `Sink` when a whole QEMUD packet is received.
 *
 * `sendAsync` sends the `size` header and then the payload.
 */

constexpr size_t kSizeSize = 4;

void encodeRequestSize(uint32_t size, uint8_t *data);
size_t decodeRequestSize(const uint8_t *const data8);

struct Parser {
    using Sink = std::function<bool(const void *data, size_t size)>;

    Parser(Sink sink);
    bool onReceive(const void *const data, size_t size);
    void saveToSnapshot(archive::IWriter &) const;
    bool loadFromSnapshot(archive::IReader &);

private:
    Sink mSink;
    std::vector<uint8_t> mBuffer;
};

void sendAsync(const void *data, size_t size, cable::ISocket &dst);

}  // namespace qemud
}  // namespace devices
}  // namespace goldfish
