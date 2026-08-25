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

#include "goldfish/archive/collections/string.h"

#include "android/status/status_macros.h"

namespace goldfish::archive {

IWriter& operator<<(IWriter& w, const std::string_view x) {
    w << x.size();
    w.Write(x.data(), x.size());
    return w;
}

absl::Status ReadValue(archive::IReader& r, std::string& dst) {
    ASSIGN_OR_RETURN(const size_t new_size, ReadOneValue<size_t>(r));

    std::string result(new_size, '?');
    if (new_size > 0) {
        RETURN_IF_ERROR(r.Read(result.data(), new_size));
    }

    dst = std::move(result);
    return absl::OkStatus();
}

}  // namespace goldfish::archive
