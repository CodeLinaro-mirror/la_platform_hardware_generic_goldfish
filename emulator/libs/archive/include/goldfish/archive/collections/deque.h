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

#pragma once

#include <deque>

#include "absl/status/status.h"

#include "android/status/status_macros.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

template <class T>
absl::Status ReadValue(archive::IReader& r, std::deque<T>& x) {
    ASSIGN_OR_RETURN(const size_t new_size, ReadOneValue<size_t>(r));

    x.clear();
    x.resize(new_size);

    for (T& v : x) {
        RETURN_IF_ERROR(ReadValue(r, v));
    }

    return absl::OkStatus();
}

template <class T>
IWriter& operator<<(IWriter& w, const std::deque<T>& x) {
    w << x.size();
    for (const T& v : x) {
        w << v;
    }

    return w;
}

}  // namespace goldfish::archive
