// Copyright 2026 The Android Open Source Project
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

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

#include "goldfish/parsing/utf8_iterator.h"

namespace goldfish::gsm {

enum class Encoding {
    GSM7,
    UCS2,
};

struct SmsAddress {
    enum class TOA : uint8_t {
        DOMESTIC = 0x81,
        INTERNATIONAL = 0x91,
        ALPHANUMERIC = 0xD1,
    };

    uint8_t size = 0;
    TOA toa = TOA::DOMESTIC;
    std::array<uint8_t, 10> data;
};

struct SmsPdu {
    std::vector<uint8_t> data;
};

absl::StatusOr<SmsAddress> ParseSmsAddress(std::string_view number);

absl::StatusOr<std::vector<SmsPdu>> SmsPdusFromUtf8(std::string_view sender,
                                                    std::string_view message);

absl::StatusOr<std::vector<SmsPdu>> SmsPdusFromBinary(std::vector<uint8_t> binary);

/*
 * GSM7 extended characters (they occupy two septets) are not allowed
 * to be split across two SMS segments.
 */
size_t CalculateGsm7NumPdus(std::string_view message, size_t septetsPerPdu);

}  // namespace goldfish::gsm
