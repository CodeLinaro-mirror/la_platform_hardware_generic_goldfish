// Copyright 2021 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <gtest/gtest.h>

#include <cstdint>

#include "goldfish/archive/zigzag.h"

TEST(zigzag, encdec) {
    namespace z = goldfish::archive::zigzag;
    const auto encdec = [](z::signed_t x) { return z::decode(z::encode(x)); };

    EXPECT_EQ(encdec(0), 0);
    EXPECT_EQ(encdec(INT64_MIN), INT64_MIN);

    static const z::signed_t values[] = {1,          127,        128,       255,
                                         256,        32767,      32768,     65535,
                                         65536,      -INT32_MIN, INT32_MAX, (UINT32_MAX - 1),
                                         UINT32_MAX, INT64_MAX};

    for (const auto x : values) {
        EXPECT_EQ(encdec(x), x);
        EXPECT_EQ(encdec(-x), -x);
    }
}
