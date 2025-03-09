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

#include "absl/strings/match.h"

namespace goldfish::devices {
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

    /**
     * @brief Constructor that initializes the string with a given value.
     *
     * @param str The initial string value. If the length of \p str exceeds the
     *            template parameter 'size', a std::length_error is thrown.
     */
    LimitedString(const std::string& str) { set(str); }

    /**
     * @brief Sets the string value.
     *
     * @param str The new string value.  If the length of \p str exceeds the
     *            template parameter 'size', a std::length_error is thrown.
     * @throws std::length_error if the input string exceeds the maximum length.
     */
    virtual void set(const std::string& str) {
        if (str.length() > size) {
            throw std::length_error("String exceeds maximum length");
        }
        mStr = str;
    }

    /**
     * @brief Returns the current string value.
     *
     * @return The current string value.
     */
    const std::string& get() const { return mStr; }

    /**
     * @brief Assignment operator.
     *
     * Assigns a new value to the LimitedString.  If the length of the input
     * string exceeds the template parameter 'size', a std::length_error is
     * thrown.
     *
     * @param str The string to assign.
     * @return A reference to the assigned LimitedString.
     * @throws std::length_error if the input string exceeds the maximum length.
     */
    LimitedString& operator=(const std::string& str) {
        set(str);
        return *this;
    }

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

  private:
    std::string mStr;  ///< The underlying string storage.
};

/**
 * @brief Custom exception class for invalid property names.
 *
 * This exception is thrown when a property name contains an invalid character.  The
 * invalid character is specified in the exception message.
 */
class InvalidPropertyName : public std::invalid_argument {
  public:
    /**
     * @brief Constructs an InvalidPropertyName exception.
     *
     * @param invalidChar The invalid character found in the property name.
     */
    InvalidPropertyName(char invalidChar)
        : std::invalid_argument("Property name contains invalid character: '" +
                                std::string(1, invalidChar) + "'") {}
};

/**
 * @brief A string class specifically designed for boot property names,
 * with a fixed maximum length and character restrictions.
 *
 * This class extends LimitedString and adds validation for boot property names.
 *
 * @tparam size The maximum allowed length of the string.
 *
 * @throws InvalidPropertyName if the input string contains invalid characters
 *         (' ', '=', '$', '*', '?', ''', '"').
 * @throws std::length_error if the input string exceeds the maximum length
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
    BootPropertyString(const std::string& str) { set(str); }

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
    void set(const std::string& str) override {
        const auto reject = absl::string_view(" =$*?'\"");
        for (char c : str) {
            if (absl::StrContains(reject, c)) {
                throw InvalidPropertyName(c);
            }
        }
        LimitedString<size>::set(str);
    }
};

}  // namespace goldfish::devices

// Hash functions for the types, so they can be used in std::unordered_map etc.
namespace std {
template <int MaxSize>
struct hash<goldfish::devices::LimitedString<MaxSize>> {
    size_t operator()(const goldfish::devices::LimitedString<MaxSize>& ls) const noexcept {
        return std::hash<string>{}(ls.get());
    }
};

template <int MaxSize>
struct hash<goldfish::devices::BootPropertyString<MaxSize>> {
    size_t operator()(const goldfish::devices::BootPropertyString<MaxSize>& ls) const noexcept {
        return std::hash<string>{}(ls.get());
    }
};
}  // namespace std