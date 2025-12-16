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
#include <string>

#include "goldfish/archive/zigzag/zigzag.h"

namespace goldfish::archive {

// See archive_unittests.cpp for usage examples
struct IReader {
    virtual ~IReader() = default;
    virtual size_t Read(void* dst, size_t size) = 0;
};

// see Writer.h for encoding explanation
inline zigzag::unsigned_t GetUnsigned(IReader& r) {
    zigzag::unsigned_t result = 0;
    unsigned shift = 0;
    constexpr unsigned kResultNumBits = sizeof(result) * CHAR_BIT;

    while (shift < kResultNumBits) {
        uint8_t b;
        if (r.Read(&b, sizeof(b)) != sizeof(b)) {
            break;
        }

        result |= (static_cast<zigzag::unsigned_t>(b & 0x7F) << shift);
        if (b >> 7) {
            shift += 7;
        } else {
            break;
        }
    }

    return result;
}

inline zigzag::signed_t GetSigned(IReader& r) {
    return zigzag::Decode(GetUnsigned(r));
}

inline float GetFloat(IReader& r) {
    float result;
    r.Read(&result, sizeof(result));
    return result;
}

inline double GetDouble(IReader& r) {
    double result;
    r.Read(&result, sizeof(result));
    return result;
}

inline std::string GetString(IReader& r) {
    const size_t size = GetUnsigned(r);
    std::string result(size, '?');
    if (r.Read(result.data(), size) == size) {
        return result;
    }
    return {};
}

}  // namespace goldfish::archive
