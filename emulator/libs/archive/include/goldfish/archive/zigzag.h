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
#include <cstdint>

namespace goldfish {
namespace archive {
namespace zigzag {
namespace {

using unsigned_t = unsigned long long;
using signed_t = signed long long;
static_assert(sizeof(unsigned_t) == sizeof(signed_t));
static_assert(sizeof(unsigned_t) == 8);

unsigned_t encode(const signed_t x) {
    return (x >> (sizeof(x) * CHAR_BIT - 1)) ^ (x << 1);
}

signed_t decode(const unsigned_t x) {
    return (x >> 1) ^ -signed_t(x & 1);
}

}  // namespace
}  // namespace zigzag
}  // namespace archive
}  // namespace goldfish
