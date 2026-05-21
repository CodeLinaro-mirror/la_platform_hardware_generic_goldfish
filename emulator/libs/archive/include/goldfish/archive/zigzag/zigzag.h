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

namespace goldfish::archive::zigzag {

using signed_t = int64_t;
static_assert(sizeof(size_t) == sizeof(signed_t));

inline size_t Encode(const signed_t x) {
    return static_cast<size_t>(x >> (sizeof(x) * CHAR_BIT - 1)) ^ (static_cast<size_t>(x) << 1);
}

inline signed_t Decode(const size_t x) {
    return static_cast<signed_t>((x >> 1) ^ -static_cast<signed_t>(x & 1));
}

}  // namespace goldfish::archive::zigzag
