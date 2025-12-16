// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <stdexcept>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace goldfish::devices::boot {
/**
 * @brief A string class with a fixed maximum length.
 *
 * This class wraps a std::string and enforces a maximum length
 * specified at compile time.  If an attempt is made to assign
 * a string exceeding this length, a std::length_error is thrown.
 *
 * @template size The maximum allowed length of the string.
 */
template <int size>
class LimitedString {
  public:
    LimitedString() = default;
    virtual ~LimitedString() = default;

    /**
     * @brief Constructor that initializes the string with a given value.
     *
     * @param str The initial string value. If the length of \p str exceeds the
     *            template parameter 'size', a std::length_error is thrown.
     */
    static absl::StatusOr<LimitedString<size>> create(const std::string& str) {
        LimitedString<size> ls;
        RETURN_IF_ERROR(ls.set(str));
        return ls;
    }

    /**
     * @brief Sets the string value.
     *
     * @param str The new string value.  If the length of \p str exceeds the
     *            template parameter 'size', a std::length_error is thrown.
     * @throws std::length_error if the input string exceeds the maximum length.
     */
    virtual absl::Status set(const std::string& str) {
        if (str.length() > size) {
            return absl::OutOfRangeError("String exceeds maximum length");
        }
        mStr = str;
        return absl::OkStatus();
    }

    /**
     * @brief Returns the current string value.
     *
     * @return The current string value.
     */
    const std::string& get() const { return mStr; }

    /**
     * @brief Implicit conversion operator to std::string.
     *
     * Allows implicit conversion of a LimitedString to a std::string.
     *
     * @return The underlying std::string value.
     */
    operator std::string() const { return mStr; }

    // Add equality operator
    bool operator==(const LimitedString& other) const { return mStr == other.mStr; }

    // Might as well add !=
    bool operator!=(const LimitedString& other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const LimitedString<size>& ls) {
        return H::combine(std::move(h), ls.mStr);
    }

  private:
    std::string mStr;  ///< The underlying string storage.
};

/**
 * @brief A string class specifically designed for boot property names,
 * with a fixed maximum length and character restrictions.
 *
 * This class extends LimitedString and adds validation for boot property names.
 *
 * @tparam size The maximum allowed length of the string.
 *
 * @returns InvalidArgumentError if the input string contains invalid characters
 *         (' ', '=', '$', '*', '?', ''', '"').
 * @returns OutOfRangeError if the input string exceeds the maximum length
 *         specified by the template parameter `size`.
 */
template <int size>
class BootPropertyString : public LimitedString<size> {
  public:
    BootPropertyString() = default;

    /**
     * @brief Constructor that initializes the string with a given value.
     *
     * @param str The initial string value.
     *
     * @throws InvalidPropertyName if the input string contains invalid characters
     *         (' ', '=', '$', '*', '?', ''', '"').
     * @throws std::length_error if the input string exceeds the maximum length
     *         specified by the template parameter `size`.
     */
    static absl::StatusOr<BootPropertyString<size>> create(const std::string& str) {
        BootPropertyString<size> bp;
        RETURN_IF_ERROR(bp.set(str));
        return bp;
    }

    /**
     * @brief Sets the string value, enforcing character restrictions.
     *
     * @param str The new string value.
     *
     * @throws InvalidPropertyName if the input string contains invalid
     *         characters (' ', '=', '$', '*', '?', ''', '"').
     * @throws std::length_error if the input string exceeds the maximum length
     *         specified by the template parameter `size`.
     */
    absl::Status set(const std::string& str) override {
        const auto reject = absl::string_view(" =$*?'\"");
        for (char c : str) {
            if (absl::StrContains(reject, c)) {
                return absl::InvalidArgumentError(
                        absl::StrCat("Property name contains invalid character: '",
                                     std::string_view(&c, 1), "'"));
            }
        }
        return LimitedString<size>::set(str);
    }
};

}  // namespace goldfish::devices::boot