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

namespace goldfish::archive {

template <>
absl::StatusOr<absl::Time> ReadValue<absl::Time>(archive::IReader& r) {
    const auto micros = ReadValue<size_t>(r);
    if (!micros.ok()) {
        return micros.status();
    }

    return absl::FromUnixMicros(*micros);
}

IWriter& operator<<(IWriter& w, const absl::Time x) {
    w << size_t(absl::ToUnixMicros(x));
    return w;
}

}  // namespace goldfish::archive
