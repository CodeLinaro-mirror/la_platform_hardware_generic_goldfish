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

#include <array>

#include "absl/status/status.h"

#include "android/status/status_macros.h"
#include "goldfish/archive/collections/span.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

template <class T, size_t SIZE>
absl::Status ReadValue(archive::IReader& r, T (&x)[SIZE]) {
    ASSIGN_OR_RETURN(const size_t size, ReadOneValue<size_t>(r));
    if (size != SIZE) {
        return absl::InvalidArgumentError("Size mismatch");
    }

    return ReadIntoMutableSpan(r, std::span<T>(x));
}

template <class T, size_t SIZE>
absl::Status ReadValue(archive::IReader& r, std::array<T, SIZE>& x) {
    ASSIGN_OR_RETURN(const size_t size, ReadOneValue<size_t>(r));
    if (size != x.size()) {
        return absl::InvalidArgumentError("Size mismatch");
    }

    return ReadIntoMutableSpan(r, std::span<T>(x));
}

template <class T, size_t SIZE>
IWriter& operator<<(IWriter& w, const T (&x)[SIZE]) {
    return w << std::span<const T>(x);
}

template <class T, size_t SIZE>
IWriter& operator<<(IWriter& w, const std::array<T, SIZE>& x) {
    return w << std::span<const T>(x);
}

}  // namespace goldfish::archive
