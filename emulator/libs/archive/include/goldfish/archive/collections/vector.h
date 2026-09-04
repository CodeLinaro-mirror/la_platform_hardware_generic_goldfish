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

#include <cstdint>
#include <vector>

#include "absl/status/status.h"

#include "android/status/status_macros.h"
#include "goldfish/archive/collections/span.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

template <class T>
absl::Status ReadValue(archive::IReader& r, std::vector<T>& x) {
    ASSIGN_OR_RETURN(const size_t new_size, ReadOneValue<size_t>(r));

    x.clear();
    x.resize(new_size);
    return ReadIntoMutableSpan(r, std::span<T>(x));
}

template <class T>
IWriter& operator<<(IWriter& w, const std::vector<T>& x) {
    return w << std::span<const T>(x);
}

}  // namespace goldfish::archive
