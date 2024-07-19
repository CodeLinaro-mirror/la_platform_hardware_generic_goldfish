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
#include "goldfish/archive/zigzag.h"

namespace goldfish {
namespace archive {

// See archive_unittests.cpp for usage examples
struct IReader {
    virtual ~IReader() {}
    virtual size_t read(void *dst, size_t size) = 0;
};

// see Writer.h for encoding explanation
inline zigzag::unsigned_t getUnsigned(IReader &r) {
    zigzag::unsigned_t result = 0;
    unsigned shift = 0;
    constexpr unsigned kResultNumBits = sizeof(result) * CHAR_BIT;

    while (shift < kResultNumBits) {
        uint8_t b;
        if (r.read(&b, sizeof(b)) != sizeof(b)) {
            break;
        }

        result |= (zigzag::unsigned_t(b & 0x7F) << shift);
        if (b >> 7) {
            shift += 7;
        } else {
            break;
        }
    }

    return result;
}

inline zigzag::signed_t getSigned(IReader& r) {
    return zigzag::decode(getUnsigned(r));
}

inline float getFloat(IReader &r) {
    float result;
    r.read(&result, sizeof(result));
    return result;
}

inline double getDouble(IReader &r) {
    double result;
    r.read(&result, sizeof(result));
    return result;
}

inline std::string getString(IReader &r) {
    const size_t size = getUnsigned(r);
    std::string result(size, '?');
    if (r.read(result.data(), size) == size) {
        return result;
    } else {
        return {};
    }
}

}  // namespace archive
}  // namespace goldfish
