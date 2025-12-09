
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
#include "android/base/storage_capacity.h"

#include <charconv>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace android::base {

StorageCapacity& StorageCapacity::operator-=(const StorageCapacity& rhs) {
    // Handle potential underflow
    if (mBytes < rhs.bytes()) {
        LOG(WARNING) << "StorageCapacity cannot be negative";
    }
    mBytes -= rhs.bytes();
    return *this;
}

StorageCapacity StorageCapacity::operator-(const StorageCapacity& rhs) const {
    // Handle potential underflow
    if (mBytes < rhs.bytes()) {
        LOG(WARNING) << "StorageCapacity cannot be negative";
    }
    unsigned long long differenceBytes = mBytes - rhs.bytes();
    return StorageCapacity(differenceBytes);
}

StorageCapacity::operator int() const {
    if (mBytes > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
        LOG(WARNING) << "StorageCapacity is too large to fit into an int";
    }
    return static_cast<int>(mBytes);
}

StorageCapacity::operator long() const {
    if (mBytes > static_cast<unsigned long long>(std::numeric_limits<long>::max())) {
        LOG(WARNING) << "StorageCapacity is too large to fit into a long";
    }
    return static_cast<long>(mBytes);
}

StorageCapacity::operator unsigned long() const {
    if (mBytes > static_cast<unsigned long long>(std::numeric_limits<unsigned long>::max())) {
        LOG(WARNING) << "StorageCapacity is too large to fit into an unsigned long";
    }
    return static_cast<unsigned long>(mBytes);
}

StorageCapacity::operator long long() const {
    if (mBytes > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
        LOG(WARNING) << "StorageCapacity is too large to fit into a long long";
    }
    return static_cast<long long>(mBytes);
}

absl::StatusOr<uint64_t> parseFromString(const std::string_view& str) {
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
        case 'k':
        case 'K':
            result *= 1024ULL;
            break;
        case 'm':
        case 'M':
            result *= 1024 * 1024ULL;
            break;
        case 'g':
        case 'G':
            result *= 1024 * 1024 * 1024ULL;
            break;
        case 't':
        case 'T':
            result *= 1024 * 1024 * 1024 * 1024ULL;
            break;
        default:
            return absl::InvalidArgumentError(absl::StrCat("Unknown label in: ", str));
        }
    }

    return result;
}

absl::StatusOr<StorageCapacity> StorageCapacity::parse(std::string_view str) {
    auto parsed_bytes = parseFromString(str);
    if (parsed_bytes.ok()) return StorageCapacity(*parsed_bytes);

    return parsed_bytes;
}

}  // namespace android::base
