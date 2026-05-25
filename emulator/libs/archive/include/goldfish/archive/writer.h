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
#include <cstdint>
#include <string_view>

#include "goldfish/archive/zigzag/zigzag.h"

namespace goldfish::archive {

// See archive_unittests.cpp for usage examples
struct IWriter {
    virtual ~IWriter() = default;
    virtual void Write(const void* src, size_t size) = 0;
};

// 7bit per byte with MSB for more bytes to follow.
IWriter& operator<<(IWriter& w, size_t x);

inline IWriter& operator<<(IWriter& w, const std::string_view x) {
    w << x.size();
    w.Write(x.data(), x.size());
    return w;
}

template <typename T>
    requires(std::same_as<T, uint8_t> || std::same_as<T, int8_t> || std::same_as<T, bool> ||
             std::same_as<T, char> || std::same_as<T, float> || std::same_as<T, double>)
inline IWriter& operator<<(IWriter& w, T x) {
    w.Write(&x, sizeof(x));
    return w;
}

template <std::unsigned_integral T>
    requires(!std::same_as<T, size_t> && !std::same_as<T, uint8_t> && !std::same_as<T, bool>)
inline IWriter& operator<<(IWriter& w, T x) {
    return (w << static_cast<size_t>(x));
}

template <std::signed_integral T>
    requires(!std::same_as<T, int8_t> && !std::same_as<T, char>)
inline IWriter& operator<<(IWriter& w, T x) {
    return (w << zigzag::Encode(x));
}

}  // namespace goldfish::archive
