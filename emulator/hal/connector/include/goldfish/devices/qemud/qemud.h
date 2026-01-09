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

namespace goldfish::devices::qemud {

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
 *
 * `encodeQemudPacket` constructs a full packet by prepending the encoded
 * size to the raw payload data.
 */
constexpr size_t kSizeSize = 4;

void EncodeRequestSize(uint32_t size, uint8_t* data);
size_t DecodeRequestSize(const uint8_t* data8);
std::string EncodeQemudPacket(std::string_view);

struct Parser {
    using Sink = std::function<bool(const void* data, size_t size)>;

    explicit Parser(Sink sink);
    bool OnReceive(const void* data, size_t size);
    void SaveToSnapshot(archive::IWriter&) const;
    bool LoadFromSnapshot(archive::IReader&);

  private:
    Sink sink_;
    std::vector<uint8_t> buffer_;
};

void SendAsync(const void* data, size_t size, cable::ISocket& dst);

}  // namespace goldfish::devices::qemud
