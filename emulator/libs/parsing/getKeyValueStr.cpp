// Copyright 2025 The Android Open Source Project
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

#include "goldfish/parsing/getKeyValueStr.h"

namespace goldfish::parsing {

std::optional<std::string_view> getKeyValueStr(const std::string_view text,
                                               const std::string_view key) {
    const size_t keySize = key.size();
    if (!keySize) {
        return std::nullopt;
    }

    const size_t textSize = text.size();

    size_t i = 0;
    while ((i = text.find(key, i)) != text.npos) {
        const size_t keyEnd = i + keySize;
        if (keyEnd >= textSize) {
            return std::nullopt;
        } else if ((!i || (text[i - 1] == ' ')) && (text[keyEnd] == '=')) {
            const size_t valueBegin = keyEnd + 1;
            const size_t valueEnd = text.find(' ', valueBegin);

            return (valueEnd == text.npos) ? text.substr(valueBegin)
                                           : text.substr(valueBegin, valueEnd - valueBegin);
        } else {
            ++i;
        }
    }

    return std::nullopt;
}

}  // namespace goldfish::parsing
