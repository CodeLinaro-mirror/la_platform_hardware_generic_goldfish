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

#include "goldfish/archive/writer.h"

#include <climits>

namespace goldfish::archive {

IWriter& operator<<(IWriter& w, size_t x) {
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

}  // namespace goldfish::archive
