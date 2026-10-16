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

#include "goldfish/archive/time.h"

#include <cstdint>
#include <utility>

#include "absl/strings/str_cat.h"

namespace goldfish::archive {

absl::Status ReadValue(archive::IReader& r, absl::Time& dst) {
    size_t micros = 0;
    if (const absl::Status s = ReadValue(r, micros); !s.ok()) {
        return s;
    }
    if (!std::in_range<int64_t>(micros)) {
        return absl::OutOfRangeError(
                absl::StrCat("Timestamp microseconds out of range for int64_t: ", micros));
    }

    dst = absl::FromUnixMicros(static_cast<int64_t>(micros));
    return absl::OkStatus();
}

IWriter& operator<<(IWriter& w, const absl::Time x) {
    w << static_cast<size_t>(absl::ToUnixMicros(x));
    return w;
}

}  // namespace goldfish::archive
