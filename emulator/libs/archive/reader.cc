/* Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/archive/reader.h"

namespace goldfish::archive {

// 7bit per byte with MSB for more bytes to follow.

template <>
absl::StatusOr<size_t> ReadValue<size_t>(archive::IReader& r) {
    size_t result = 0;
    unsigned shift = 0;
    constexpr unsigned kResultNumBits = sizeof(result) * CHAR_BIT;

    while (shift < kResultNumBits) {
        uint8_t b;
        if (const absl::Status s = r.Read(&b, sizeof(b)); !s.ok()) {
            return s;
        }

        result |= (static_cast<size_t>(b & 0x7F) << shift);
        if (b >> 7) {
            shift += 7;
        } else {
            break;
        }
    }

    return result;
}

template <>
absl::StatusOr<std::string> ReadValue<std::string>(archive::IReader& r) {
    const auto size = ReadValue<size_t>(r);
    if (!size.ok()) {
        return size.status();
    }

    std::string result(*size, '?');
    if (const absl::Status s = r.Read(result.data(), result.size()); !s.ok()) {
        return s;
    }

    return result;
}

}  // namespace goldfish::archive
