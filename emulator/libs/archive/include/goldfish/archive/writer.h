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
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "goldfish/archive/zigzag/zigzag.h"

namespace goldfish::archive {

// See archive_unittests.cpp for usage examples
struct IWriter {
    virtual ~IWriter() = default;
    virtual void Write(const void* src, size_t size) = 0;
};

// 7bit per byte with MSB for more bytes to follow.
inline IWriter& operator<<(IWriter& w, zigzag::unsigned_t x) {
    uint8_t buf[(sizeof(x) * CHAR_BIT + 7 - 1) / 7];

    unsigned len = 0;
    while (true) {
        const decltype(x) high7 = x >> 7;
        const unsigned low7 = x & 0x7FU;
        buf[len] = low7 | (static_cast<unsigned>(high7 > 0) << 7);
        ++len;
        if (high7) {
            x = high7;
        } else {
            break;
        }
    }

    w.Write(buf, len);
    return w;
}

inline IWriter& operator<<(IWriter& w, const zigzag::signed_t x) {
    return (w << zigzag::Encode(x));
}

inline IWriter& operator<<(IWriter& w, const unsigned char x) {
    return (w << static_cast<zigzag::unsigned_t>(x));
}

inline IWriter& operator<<(IWriter& w, const unsigned short x) {
    return (w << static_cast<zigzag::unsigned_t>(x));
}

inline IWriter& operator<<(IWriter& w, const unsigned int x) {
    return (w << static_cast<zigzag::unsigned_t>(x));
}

inline IWriter& operator<<(IWriter& w, const unsigned long x) {
    return (w << static_cast<zigzag::unsigned_t>(x));
}

inline IWriter& operator<<(IWriter& w, const signed char x) {
    return (w << static_cast<zigzag::signed_t>(x));
}

inline IWriter& operator<<(IWriter& w, const signed short x) {
    return (w << static_cast<zigzag::signed_t>(x));
}

inline IWriter& operator<<(IWriter& w, const signed int x) {
    return (w << static_cast<zigzag::signed_t>(x));
}

inline IWriter& operator<<(IWriter& w, const signed long x) {
    return (w << static_cast<zigzag::signed_t>(x));
}

inline IWriter& operator<<(IWriter& w, const bool x) {
    return (w << static_cast<zigzag::unsigned_t>(x));
}

inline IWriter& operator<<(IWriter& w, const char x) {
    return (w << static_cast<zigzag::signed_t>(x));
}

inline IWriter& operator<<(IWriter& w, const float x) {
    w.Write(&x, sizeof(x));
    return w;
}

inline IWriter& operator<<(IWriter& w, const double x) {
    w.Write(&x, sizeof(x));
    return w;
}

inline IWriter& operator<<(IWriter& w, const std::string_view x) {
    w << x.size();
    w.Write(x.data(), x.size());
    return w;
}

}  // namespace goldfish::archive
