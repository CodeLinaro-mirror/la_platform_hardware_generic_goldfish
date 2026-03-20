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
#include "goldfish/parsing/hexbin.h"

namespace goldfish::parsing {
namespace {
int ParseOne(const int c) {
    if (c >= 'A') {
        if (c <= 'F') {
            return c - 'A' + 10;
        } else if ((c >= 'a') && (c <= 'f')) {
            return c - 'a' + 10;
        } else {
            return -1;
        }
    } else if ((c >= '0') && (c <= '9')) {
        return c - '0';
    } else {
        return -1;
    }
}
}  // namespace

std::optional<std::vector<uint8_t>> HexToBin(const std::string_view hex) {
    if (hex.size() % 2U) {
        return std::nullopt;
    }

    const size_t numBytes = hex.size() / 2U;
    std::vector<uint8_t> binary(numBytes);

    for (size_t i = 0; i < numBytes; ++i) {
        const int high = ParseOne(hex[i + i]);
        if (high < 0) {
            return std::nullopt;
        }

        const int low = ParseOne(hex[i + i + 1]);
        if (low < 0) {
            return std::nullopt;
        }

        binary[i] = (high << 4) | low;
    }

    return binary;
}

std::string BinToHex(const std::span<const uint8_t> bin) {
    static const char kHexDigits[] = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    static_assert(std::size(kHexDigits) == 16);

    std::string result(bin.size() * 2U, '?');

    for (size_t i = 0; i < bin.size(); ++i) {
        result[i + i] = kHexDigits[bin[i] >> 4U];
        result[i + i + 1] = kHexDigits[bin[i] & 0xFU];
    }

    return result;
}

}  // namespace goldfish::parsing
