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
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/status/status_macros.h"
#include "goldfish/archive/collections/span.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

template <class T>
absl::Status ReadValue(archive::IReader& r, std::deque<T>& x) {
    ASSIGN_OR_RETURN(const size_t new_size, ReadOneValue<size_t>(r));
    if (new_size > kMaxArchiveElements || new_size > kMaxArchiveAllocationBytes / sizeof(T)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "Archived deque size (", new_size, ") exceeds maximum limit (elements: ",
                kMaxArchiveElements, ", bytes: ", kMaxArchiveAllocationBytes, ")"));
    }

    std::deque<T> loaded;
    for (size_t i = 0; i < new_size; ++i) {
        T v{};
        RETURN_IF_ERROR(ReadValue(r, v));
        loaded.push_back(std::move(v));
    }

    x = std::move(loaded);
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
