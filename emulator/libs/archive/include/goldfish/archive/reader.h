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

template <typename T>
absl::StatusOr<T> ReadValue(archive::IReader& r) = delete;

template <>
absl::StatusOr<size_t> ReadValue<size_t>(archive::IReader& r);
template <>
absl::StatusOr<std::string> ReadValue<std::string>(archive::IReader& r);

template <typename T>
    requires(std::same_as<T, uint8_t> || std::same_as<T, int8_t> || std::same_as<T, bool> ||
             std::same_as<T, char> || std::same_as<T, float> || std::same_as<T, double>)
absl::StatusOr<T> ReadValue(archive::IReader& r) {
    T result;
    if (const absl::Status s = r.Read(&result, sizeof(result)); !s.ok()) {
        return s;
    }
    return result;
}

template <std::unsigned_integral T>
    requires(!std::same_as<T, size_t> && !std::same_as<T, uint8_t> && !std::same_as<T, bool>)
absl::StatusOr<T> ReadValue(archive::IReader& r) {
    const absl::StatusOr<size_t> raw = ReadValue<size_t>(r);
    if (!raw.ok()) return raw.status();

    if (!std::in_range<T>(*raw)) {
        return absl::OutOfRangeError("Unsigned value out of bounds for target type");
    }

    return static_cast<T>(*raw);
}

template <std::signed_integral T>
    requires(!std::same_as<T, int8_t> && !std::same_as<T, char>)
absl::StatusOr<T> ReadValue(archive::IReader& r) {
    const absl::StatusOr<size_t> raw = ReadValue<size_t>(r);
    if (!raw.ok()) return raw.status();

    const auto decoded = zigzag::Decode(*raw);
    if (!std::in_range<T>(decoded)) {
        return absl::OutOfRangeError("Signed value out of bounds for target type");
    }

    return static_cast<T>(decoded);
}

inline absl::Status ReadValue(archive::IReader&) {
    return absl::OkStatus();
}

template <typename T, typename... Args>
absl::Status ReadValue(archive::IReader& r, T& first, Args&... rest) {
    // Call your original single-value template
    absl::StatusOr<T> result = ReadValue<T>(r);
    if (result.ok()) {
        first = *std::move(result);
        return ReadValue(r, rest...);
    } else {
        return result.status();
    }
}

}  // namespace goldfish::archive
