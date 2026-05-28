
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
#include "goldfish/file/storage_capacity.h"

#include <charconv>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace android::base {

namespace {

absl::StatusOr<uint64_t> parseFromString(const std::string_view str) {
    size_t processed_chars = 0;
    uint64_t result = 0;

    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), result, 10);
    if (ec != std::errc{}) {
        if (ec == std::errc::invalid_argument)
            return absl::InvalidArgumentError(absl::StrCat("Invalid number: ", str));
        else if (ec == std::errc::result_out_of_range)
            return absl::InvalidArgumentError(absl::StrCat("Number out of range: ", str));
        else
            return absl::UnknownError(absl::StrCat("Unknown error for: ", str));
    }

    // Move the pointer past the processed characters
    processed_chars = ptr - str.data();

    if (processed_chars < str.size()) {
        switch (*ptr) {
        case 'b':
        case 'B':
            break;
        case 'k':
        case 'K':
            result <<= 10;
            break;
        case 'm':
        case 'M':
            result <<= 20;
            break;
        case 'g':
        case 'G':
            result <<= 30;
            break;
        case 't':
        case 'T':
            result <<= 40;
            break;
        default:
            return absl::InvalidArgumentError(absl::StrCat("Unknown label in: ", str));
        }
    }

    return result;
}

}  // namespace

StorageCapacity& StorageCapacity::operator-=(const StorageCapacity& rhs) {
    // Handle potential underflow
    if (bytes_ < rhs.Bytes()) {
        LOG(WARNING) << "StorageCapacity cannot be negative";
    }
    bytes_ -= rhs.Bytes();
    return *this;
}

StorageCapacity StorageCapacity::operator-(const StorageCapacity& rhs) const {
    // Handle potential underflow
    if (bytes_ < rhs.Bytes()) {
        LOG(WARNING) << "StorageCapacity cannot be negative";
    }
    unsigned long long differenceBytes = bytes_ - rhs.Bytes();
    return StorageCapacity(differenceBytes);
}

std::string StorageCapacity::String() const {
    constexpr unsigned kShiftT = 40;
    constexpr unsigned kShiftG = 30;
    constexpr unsigned kShiftM = 20;
    constexpr unsigned kShiftK = 10;

    const auto is_multiple_of = [](const ValueType value, const unsigned shift) -> ValueType {
        constexpr ValueType kOne = 1;
        const ValueType n = value >> shift;
        if (n && !(value & ((kOne << shift) - 1U))) {
            return n;
        } else {
            return 0;
        }
    };

    if (ValueType n = is_multiple_of(bytes_, kShiftT)) {
        return absl::StrCat(n, "T");
    }
    if (ValueType n = is_multiple_of(bytes_, kShiftG)) {
        return absl::StrCat(n, "G");
    }
    if (ValueType n = is_multiple_of(bytes_, kShiftM)) {
        return absl::StrCat(n, "M");
    }
    if (ValueType n = is_multiple_of(bytes_, kShiftK)) {
        return absl::StrCat(n, "K");
    }

    return absl::StrCat(bytes_, "B");
}

absl::StatusOr<StorageCapacity> StorageCapacity::Parse(std::string_view str) {
    auto parsed_bytes = parseFromString(str);
    if (parsed_bytes.ok()) return StorageCapacity(*parsed_bytes);

    return parsed_bytes;
}

}  // namespace android::base
