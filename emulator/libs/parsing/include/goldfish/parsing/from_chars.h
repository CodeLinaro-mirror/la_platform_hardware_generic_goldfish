/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <charconv>
#include <optional>
#include <string_view>

namespace goldfish::parsing {

template <class T>
std::optional<T> FromChars(const std::string_view str, const int base = 10) {
    if (str.empty()) {
        return std::nullopt;
    }

    T value;
    const auto [ptr, ec] = std::from_chars(&*str.begin(), &*str.end(), value, base);
    if ((ec == std::errc()) && (ptr == &*str.end())) {
        return value;
    }
    return std::nullopt;
}

template <class T>
std::optional<T> FromCharsF(const std::string_view str) {
    if (str.empty()) {
        return std::nullopt;
    }

    T value;
    const auto [ptr, ec] = std::from_chars(&*str.begin(), &*str.end(), value);
    if ((ec == std::errc()) && (ptr == &*str.end())) {
        return value;
    }
    return std::nullopt;
}

}  // namespace goldfish::parsing
