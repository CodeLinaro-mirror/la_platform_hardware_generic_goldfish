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

#include <cmath>
#include <span>

#include "absl/status/status.h"

#include "android/status/status_macros.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

// Upper bounds for deserializing dynamic containers from snapshots to prevent
// corrupted varint length prefixes from triggering std::bad_alloc aborts.
inline constexpr size_t kMaxArchiveAllocationBytes = 64ULL << 20;  // 64 MiB
inline constexpr size_t kMaxArchiveElements = 1ULL << 20;          // 1M elements

template <class T>
absl::Status ReadIntoMutableSpan(archive::IReader& r, std::span<T> x) {
    if constexpr (std::same_as<T, char> || std::same_as<T, int8_t> || std::same_as<T, uint8_t>) {
        if (!x.empty()) {
            return r.Read(x.data(), x.size() * sizeof(T));
        }
    } else if constexpr (std::same_as<T, float> || std::same_as<T, double>) {
        if (!x.empty()) {
            RETURN_IF_ERROR(r.Read(x.data(), x.size() * sizeof(T)));
            for (const T& v : x) {
                if (std::isnan(v)) {
                    return absl::InvalidArgumentError("NaN floating point value in archive");
                }
            }
        }
    } else {
        for (T& v : x) {
            RETURN_IF_ERROR(ReadValue(r, v));
        }
    }

    return absl::OkStatus();
}

template <class T>
IWriter& operator<<(IWriter& w, std::span<const T> x) {
    w << x.size();

    if constexpr (std::same_as<T, char> || std::same_as<T, int8_t> || std::same_as<T, uint8_t> ||
                  std::same_as<T, float> || std::same_as<T, double>) {
        if (!x.empty()) {
            w.Write(x.data(), x.size() * sizeof(T));
        }
    } else {
        for (const T& v : x) {
            w << v;
        }
    }

    return w;
}

}  // namespace goldfish::archive
