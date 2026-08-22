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

#include <set>

#include "absl/status/status.h"

#include "android/status/status_macros.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

template <class T, class CMP>
absl::Status ReadValue(archive::IReader& r, std::set<T, CMP>& x) {
    ASSIGN_OR_RETURN(size_t new_size, ReadOneValue<size_t>(r));

    x.clear();
    for (size_t n = new_size; n; --n) {
        ASSIGN_OR_RETURN(T val, ReadOneValue<T>(r));

        if (!x.insert(std::move(val)).second) {
            return absl::InvalidArgumentError("Duplicate key found while deserializing std::set");
        }
    }

    return absl::OkStatus();
}

template <class T, class CMP>
IWriter& operator<<(IWriter& w, const std::set<T, CMP>& x) {
    w << x.size();
    for (const T& v : x) {
        w << v;
    }

    return w;
}

}  // namespace goldfish::archive
