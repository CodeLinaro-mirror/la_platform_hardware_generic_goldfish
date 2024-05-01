
// Copyright (C) 2024 The Android Open Source Project
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

#include "absl/status/statusor.h"
#include "aemu/base/Log.h"

#include <iostream>
#include <string_view>

namespace android::base {

/**
 * @brief Class representing storage capacity.
 */
class StorageCapacity {
public:
  /**
   * @brief Enum for representing storage capacity units.
   */
  enum class Unit {
    B,   ///< Bytes
    KiB, ///< Kilobytes
    MiB, ///< Megabytes
    GiB, ///< Gigabytes
    TiB  ///< Terabytes
  };

  constexpr StorageCapacity() : mBytes(0) {}

  /**
   * @brief Constructor taking bytes as input.
   * @param bytes The storage capacity in bytes.
   */
  constexpr StorageCapacity(unsigned long long bytes) { mBytes = bytes; }

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
    case Unit::B:
      mBytes = bytes;
      break;
    case Unit::KiB:
      mBytes = bytes * 1024ULL;
      break;
    case Unit::MiB:
      mBytes = bytes * 1024ULL * 1024ULL;
      break;
    case Unit::GiB:
      mBytes = bytes * 1024ULL * 1024ULL * 1024ULL;
      break;
    case Unit::TiB:
      mBytes = bytes * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
      break;
    }
  }
  ~StorageCapacity() = default;

  /**
   * @brief Get the storage capacity in bytes.
   * @return The storage capacity in bytes.
   */
  unsigned long long bytes() const { return mBytes; }

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
  StorageCapacity align(StorageCapacity align) {
    auto bytes = ((mBytes + align.bytes() - 1) / align.bytes()) * align.bytes();
    return StorageCapacity(bytes);
  }

  /**
   * @brief Stream insertion operator (<<) for outputting StorageCapacity
   * objects.
   *
   * @param os The output stream to write to.
   * @param capacity The StorageCapacity object to output.
   * @return A reference to the output stream.
   */
  friend std::ostream &operator<<(std::ostream &os,
                                  const StorageCapacity &capacity) {
    os << capacity.string(); // Utilize the string() method
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
  std::string string() const {
    const unsigned long long KB = 1024;
    const unsigned long long MB = 1024 * KB;
    const unsigned long long GB = 1024 * MB;
    const unsigned long long TB = 1024 * GB;

    double value = mBytes;

    // Determine the largest appropriate unit

    if (value >= TB) {
      value /= TB;
      return absl::StrFormat("%.2f TiB", value);
    } else if (value >= GB) {
      value /= GB;
      return absl::StrFormat("%.2f GiB", value);
    } else if (value >= MB) {
      value /= MB;
      return absl::StrFormat("%.2f MiB", value);
    } else if (value >= KB) {
      value /= KB;
      return absl::StrFormat("%.2f KiB", value);
    } else {
      return absl::StrFormat("%d B", static_cast<int>(value));
    }
  }

  /**
   * @brief Parses a string representation of storage capacity.
   * @param str The string representation of storage capacity.
   * @return An absl::StatusOr containing the parsed storage capacity on
   * success, or an error status on failure.
   */
  static absl::StatusOr<StorageCapacity> parse(std::string_view str);

  // Spaceship operator for comparison
  auto operator<=>(const StorageCapacity &rhs) const {
    return bytes() <=> rhs.bytes();
  };

  // Equality operators
  bool operator==(const StorageCapacity &rhs) const {
    return bytes() == rhs.bytes();
  }

  /**
   * @brief Multiplies a StorageCapacity object by a scalar value.
   *
   * @param scalar The scalar value to multiply by.
   * @return A new StorageCapacity object with the capacity multiplied.
   */
  StorageCapacity operator*(unsigned long long scalar) const {
    unsigned long long multipliedBytes = mBytes * scalar;
    return StorageCapacity(multipliedBytes);
  }

  /**
   * @brief Adds two StorageCapacity objects.
   *
   * @param rhs The StorageCapacity object to add.
   * @return A new StorageCapacity object representing the sum of the
   * capacities.
   */
  StorageCapacity operator+(const StorageCapacity &rhs) const {
    unsigned long long totalBytes = mBytes + rhs.bytes();
    return StorageCapacity(totalBytes);
  }

  /**
   * @brief Adds another StorageCapacity to the current one (in-place).
   *
   * @param rhs The StorageCapacity object to add.
   * @return A reference to the modified StorageCapacity object (`*this`).
   */
  StorageCapacity &operator+=(const StorageCapacity &rhs) {
    mBytes += rhs.bytes();
    return *this;
  }

  /**
   * @brief Subtracts another StorageCapacity from the current one (in-place).
   *
   * @param rhs The StorageCapacity object to subtract.
   * @return A reference to the modified StorageCapacity object (`*this`).
   */
  StorageCapacity &operator-=(const StorageCapacity &rhs) {
    // Handle potential underflow
    if (mBytes < rhs.bytes()) {
      dwarning("StorageCapacity cannot be negative");
    }
    mBytes -= rhs.bytes();
    return *this;
  }

  /**
   * @brief Subtracts two StorageCapacity objects.
   *
   * @param rhs The StorageCapacity object to subtract.
   * @return A new StorageCapacity object representing the difference between
   * the capacities.
   */
  StorageCapacity operator-(const StorageCapacity &rhs) const {
    // Handle potential underflow
    if (mBytes < rhs.bytes()) {
      dwarning("StorageCapacity cannot be negative");
    }
    unsigned long long differenceBytes = mBytes - rhs.bytes();
    return StorageCapacity(differenceBytes);
  }

  // Conversion to int
  explicit operator int() const {
    if (mBytes >
        static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
      dwarning("StorageCapacity is too large to fit into an int");
    }
    return static_cast<int>(mBytes);
  }

  // Conversion to long
  explicit operator long() const {
    if (mBytes >
        static_cast<unsigned long long>(std::numeric_limits<long>::max())) {
      dwarning("StorageCapacity is too large to fit into a long");
    }
    return static_cast<long>(mBytes);
  }

  // Conversion to unsigned long
  explicit operator unsigned long() const {
    if (mBytes > static_cast<unsigned long long>(
                     std::numeric_limits<unsigned long>::max())) {
      dwarning("StorageCapacity is too large to fit into an unsigned long");
    }
    return static_cast<unsigned long>(mBytes);
  }

  // Conversion to long long
  explicit operator long long() const {
    if (mBytes > static_cast<unsigned long long>(
                     std::numeric_limits<long long>::max())) {
      dwarning("StorageCapacity is too large to fit into a long long");
    }
    return static_cast<long long>(mBytes);
  }

  // Conversion to unsigned long long
  explicit operator unsigned long long() const { return mBytes; }

private:
  unsigned long long mBytes; ///< The storage capacity in bytes.
};

// User-defined literals
/**
 * @brief User-defined literal for bytes (B) following IEEE 1541.
 * @param value The value to convert to bytes.
 * @return StorageCapacity object representing the capacity in bytes.
 */
constexpr StorageCapacity operator"" _B(unsigned long long value) {
  return StorageCapacity(value);
}

/**
 * @brief User-defined literal for kilobytes (KiB) following IEEE 1541.
 * @param value The value to convert to kilobytes.
 * @return StorageCapacity object representing the capacity in kilobytes.
 */
constexpr StorageCapacity operator"" _KiB(unsigned long long value) {
  return StorageCapacity(value, StorageCapacity::Unit::KiB);
}

/**
 * @brief User-defined literal for megabytes (MiB) following IEEE 1541.
 * @param value The value to convert to megabytes.
 * @return StorageCapacity object representing the capacity in megabytes.
 */
constexpr StorageCapacity operator"" _MiB(unsigned long long value) {
  return StorageCapacity(value, StorageCapacity::Unit::MiB);
}

/**
 * @brief User-defined literal for gigabytes (GiB) following IEEE 1541.
 * @param value The value to convert to gigabytes.
 * @return StorageCapacity object representing the capacity in gigabytes.
 */
constexpr StorageCapacity operator"" _GiB(unsigned long long value) {
  return StorageCapacity(value, StorageCapacity::Unit::GiB);
}

/**
 * @brief User-defined literal for teraabytes (TiB) following IEEE 1541.
 * @param value The value to convert to gigabytes.
 * @return StorageCapacity object representing the capacity in gigabytes.
 */
constexpr StorageCapacity operator"" _TiB(unsigned long long value) {
  return StorageCapacity(value, StorageCapacity::Unit::TiB);
}

} // namespace android::base