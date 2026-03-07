
// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License";
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

#include <cstdint>
#include <iostream>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

namespace android::base {

/**
 * @brief Class representing storage capacity.
 */
class StorageCapacity {
  public:
    template <typename Sink>
    friend void AbslStringify(Sink& sink, const StorageCapacity& sc) {
        absl::Format(&sink, "%s", sc.String());
    }

    /**
     * @brief Enum for representing storage capacity units.
     */
    enum class Unit : uint8_t {
        kB,    ///< Bytes
        kKiB,  ///< Kilobytes
        kMiB,  ///< Megabytes
        kGiB,  ///< Gigabytes
        kTiB   ///< Terabytes
    };

    constexpr StorageCapacity() : bytes_(0) {}

    /**
     * @brief Constructor taking bytes as input.
     * @param bytes The storage capacity in bytes.
     */
    constexpr StorageCapacity(unsigned long long bytes) : bytes_(bytes) {}  // NOLINT

    /**
     * @brief Constructor taking a raw value and unit for storage capacity.
     *
     * This constructor allows you to specify a raw value for storage capacity,
     * along with the units of that value. The internal representation will
     * be in bytes.
     *
     * @param value The raw numeric value representing the storage capacity.
     * @param unit The unit of the provided `value` (B, KiB, MiB, GiB).
     */
    constexpr StorageCapacity(unsigned long long bytes, Unit unit) {
        switch (unit) {
        case Unit::kB:
            bytes_ = bytes;
            break;
        case Unit::kKiB:
            bytes_ = bytes * 1024ULL;
            break;
        case Unit::kMiB:
            bytes_ = bytes * 1024ULL * 1024ULL;
            break;
        case Unit::kGiB:
            bytes_ = bytes * 1024ULL * 1024ULL * 1024ULL;
            break;
        case Unit::kTiB:
            bytes_ = bytes * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
            break;
        }
    }
    ~StorageCapacity() = default;

    /**
     * @brief Get the storage capacity in bytes.
     * @return The storage capacity in bytes.
     */
    unsigned long long Bytes() const { return bytes_; }

    /**
     * @brief Aligns the current storage capacity to a multiple of the provided
     * alignment.
     *
     * This function rounds the current capacity up to the nearest multiple of the
     * given alignment and returns a new, aligned StorageCapacity object.
     *
     * @param align The desired alignment (a StorageCapacity object).
     * @return A new StorageCapacity object with its capacity aligned.
     */
    StorageCapacity Align(StorageCapacity align) const {
        auto bytes = ((bytes_ + align.Bytes() - 1) / align.Bytes()) * align.Bytes();
        return StorageCapacity(bytes);  // NOLINT
    }

    /**
     * @brief Stream insertion operator (<<) for outputting StorageCapacity
     * objects.
     *
     * @param os The output stream to write to.
     * @param capacity The StorageCapacity object to output.
     * @return A reference to the output stream.
     */
    friend std::ostream& operator<<(std::ostream& os, const StorageCapacity& capacity) {
        os << capacity.String();  // Utilize the string() method
        return os;
    }

    /**
     * @brief Returns a human-readable string representation of the storage
     * capacity.
     *
     * Automatically selects the most appropriate unit (B, KiB, MiB, GiB) for
     * display.
     *
     * @return A string representing the capacity.
     */
    std::string String() const {
        const unsigned long long kilo_byte = 1024;
        const unsigned long long mega_byte = 1024 * kilo_byte;
        const unsigned long long giga_byte = 1024 * mega_byte;
        const unsigned long long tera_byte = 1024 * giga_byte;

        auto value = static_cast<double>(bytes_);

        // Determine the largest appropriate unit

        if (value >= tera_byte) {
            value /= tera_byte;
            return absl::StrFormat("%.2f TiB", value);
        }
        if (value >= giga_byte) {
            value /= giga_byte;
            return absl::StrFormat("%.2f GiB", value);
        }
        if (value >= mega_byte) {
            value /= mega_byte;
            return absl::StrFormat("%.2f MiB", value);
        }
        if (value >= kilo_byte) {
            value /= kilo_byte;
            return absl::StrFormat("%.2f KiB", value);
        }
        return absl::StrFormat("%d B", static_cast<int>(value));
    }

    /**
     * @brief Parses a string representation of storage capacity.
     * @param str The string representation of storage capacity.
     * @return An absl::StatusOr containing the parsed storage capacity on
     * success, or an error status on failure.
     */
    static absl::StatusOr<StorageCapacity> Parse(std::string_view str);

    // Equality operators
    bool operator==(const StorageCapacity& rhs) const { return Bytes() == rhs.Bytes(); }

    bool operator!=(const StorageCapacity& rhs) const { return Bytes() != rhs.Bytes(); }
    bool operator<(const StorageCapacity& rhs) const { return Bytes() < rhs.Bytes(); }
    bool operator<=(const StorageCapacity& rhs) const { return Bytes() <= rhs.Bytes(); }
    bool operator>(const StorageCapacity& rhs) const { return Bytes() > rhs.Bytes(); }
    bool operator>=(const StorageCapacity& rhs) const { return Bytes() >= rhs.Bytes(); }

    /**
     * @brief Multiplies a StorageCapacity object by a scalar value.
     *
     * @param scalar The scalar value to multiply by.
     * @return A new StorageCapacity object with the capacity multiplied.
     */
    StorageCapacity operator*(unsigned long long scalar) const {
        const unsigned long long multiplied_bytes = bytes_ * scalar;
        return StorageCapacity(multiplied_bytes);  // NOLINT
    }

    /**
     * @brief Adds two StorageCapacity objects.
     *
     * @param rhs The StorageCapacity object to add.
     * @return A new StorageCapacity object representing the sum of the
     * capacities.
     */
    StorageCapacity operator+(const StorageCapacity& rhs) const {
        const unsigned long long total_bytes = bytes_ + rhs.Bytes();
        return StorageCapacity(total_bytes);  // NOLINT
    }

    /**
     * @brief Adds another StorageCapacity to the current one (in-place).
     *
     * @param rhs The StorageCapacity object to add.
     * @return A reference to the modified StorageCapacity object (`*this`).
     */
    StorageCapacity& operator+=(const StorageCapacity& rhs) {
        bytes_ += rhs.Bytes();
        return *this;
    }

    /**
     * @brief Subtracts another StorageCapacity from the current one (in-place).
     *
     * @param rhs The StorageCapacity object to subtract.
     * @return A reference to the modified StorageCapacity object (`*this`).
     */
    StorageCapacity& operator-=(const StorageCapacity& rhs);

    /**
     * @brief Subtracts two StorageCapacity objects.
     *
     * @param rhs The StorageCapacity object to subtract.
     * @return A new StorageCapacity object representing the difference between
     * the capacities.
     */
    StorageCapacity operator-(const StorageCapacity& rhs) const;

    // Conversion to int
    explicit operator int() const;

    // Conversion to long
    explicit operator long() const;

    // Conversion to unsigned long
    explicit operator unsigned long() const;

    // Conversion to long long
    explicit operator long long() const;

    // Conversion to unsigned long long
    explicit operator unsigned long long() const { return bytes_; }

  private:
    uint64_t bytes_;  ///< The storage capacity in bytes.
};

// User-defined literals
/**
 * @brief User-defined literal for bytes (B) following IEEE 1541.
 * @param value The value to convert to bytes.
 * @return StorageCapacity object representing the capacity in bytes.
 */
constexpr StorageCapacity operator""_B(unsigned long long value) {
    return StorageCapacity(value);  // NOLINT
}

/**
 * @brief User-defined literal for kilobytes (KiB) following IEEE 1541.
 * @param value The value to convert to kilobytes.
 * @return StorageCapacity object representing the capacity in kilobytes.
 */
constexpr StorageCapacity operator""_KiB(unsigned long long value) {
    return StorageCapacity(value, StorageCapacity::Unit::kKiB);  // NOLINT
}

/**
 * @brief User-defined literal for megabytes (kMiB) following IEEE 1541.
 * @param value The value to convert to megabytes.
 * @return StorageCapacity object representing the capacity in megabytes.
 */
constexpr StorageCapacity operator""_MiB(unsigned long long value) {
    return StorageCapacity(value, StorageCapacity::Unit::kMiB);  // NOLINT
}

/**
 * @brief User-defined literal for gigabytes (GiB) following IEEE 1541.
 * @param value The value to convert to gigabytes.
 * @return StorageCapacity object representing the capacity in gigabytes.
 */
constexpr StorageCapacity operator""_GiB(unsigned long long value) {
    return StorageCapacity(value, StorageCapacity::Unit::kGiB);  // NOLINT
}

/**
 * @brief User-defined literal for teraabytes (TiB) following IEEE 1541.
 * @param value The value to convert to gigabytes.
 * @return StorageCapacity object representing the capacity in gigabytes.
 */
constexpr StorageCapacity operator""_TiB(unsigned long long value) {
    return StorageCapacity(value, StorageCapacity::Unit::kTiB);  // NOLINT
}

}  // namespace android::base
