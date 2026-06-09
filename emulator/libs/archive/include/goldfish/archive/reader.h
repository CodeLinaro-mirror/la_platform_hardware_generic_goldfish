/* Copyright (C) 2024 The Android Open Source Project
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
#include <concepts>
#include <cstddef>
#include <string>
#include <utility>

#include "absl/status/statusor.h"

#include "goldfish/archive/zigzag/zigzag.h"

namespace goldfish::archive {

// See archive_unittests.cpp for usage examples
struct IReader {
    virtual ~IReader() = default;
    virtual absl::Status Read(void* dst, size_t size) = 0;
};

absl::Status ReadValue(IReader& r, size_t&);
absl::Status ReadValue(IReader& r, std::string&);

template <typename T>
    requires(std::same_as<T, uint8_t> || std::same_as<T, int8_t> || std::same_as<T, bool> ||
             std::same_as<T, char> || std::same_as<T, float> || std::same_as<T, double>)
absl::Status ReadValue(IReader& r, T& dst) {
    return r.Read(&dst, sizeof(dst));
}

template <std::unsigned_integral T>
    requires(!std::same_as<T, size_t> && !std::same_as<T, uint8_t> && !std::same_as<T, bool>)
absl::Status ReadValue(IReader& r, T& dst) {
    size_t val;
    if (const absl::Status s = ReadValue(r, val); !s.ok()) {
        return s;
    }

    if (!std::in_range<T>(val)) {
        return absl::OutOfRangeError("Unsigned value out of bounds for target type");
    }

    dst = static_cast<T>(val);
    return absl::OkStatus();
}

template <std::signed_integral T>
    requires(!std::same_as<T, int8_t> && !std::same_as<T, char>)
absl::Status ReadValue(IReader& r, T& dst) {
    size_t val;
    if (const absl::Status s = ReadValue(r, val); !s.ok()) {
        return s;
    }

    const auto decoded = zigzag::Decode(val);
    if (!std::in_range<T>(decoded)) {
        return absl::OutOfRangeError("Signed value out of bounds for target type");
    }

    dst = static_cast<T>(decoded);
    return absl::OkStatus();
}

inline absl::Status ReadValue(IReader&) {
    return absl::OkStatus();
}

template <typename T, typename... Args>
absl::Status ReadValue(IReader& r, T& first, Args&... rest) {
    if (const absl::Status s = ReadValue(r, first); s.ok()) {
        return ReadValue(r, rest...);
    } else {
        return s;
    }
}

template <class T>
absl::StatusOr<T> ReadOneValue(IReader& r) {
    T loaded = {};
    if (const absl::Status s = ReadValue(r, loaded); s.ok()) {
        return loaded;
    } else {
        return s;
    }
}

}  // namespace goldfish::archive
