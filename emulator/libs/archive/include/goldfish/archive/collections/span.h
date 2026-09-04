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

#include <span>

#include "absl/status/status.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

template <class T>
absl::Status ReadIntoMutableSpan(archive::IReader& r, std::span<T> x) {
    if constexpr (std::same_as<T, char> || std::same_as<T, int8_t> || std::same_as<T, uint8_t> ||
                  std::same_as<T, float> || std::same_as<T, double>) {
        if (!x.empty()) {
            return r.Read(x.data(), x.size() * sizeof(T));
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
