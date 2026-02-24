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

#include "goldfish/parsing/get_key_value_str.h"

namespace goldfish::parsing {

std::optional<std::string_view> GetKeyValueStr(const std::string_view text,
                                               const std::string_view key) {
    const size_t key_size = key.size();
    if (!key_size) {
        return std::nullopt;
    }

    const size_t text_size = text.size();

    size_t i = 0;
    while ((i = text.find(key, i)) != std::string_view::npos) {
        const size_t key_end = i + key_size;
        if (key_end >= text_size) {
            return std::nullopt;
        }
        if ((!i || (text[i - 1] == ' ')) && (text[key_end] == '=')) {
            const size_t value_begin = key_end + 1;
            const size_t value_end = text.find(' ', value_begin);

            return (value_end == std::string_view::npos)
                           ? text.substr(value_begin)
                           : text.substr(value_begin, value_end - value_begin);
        }
        ++i;
    }

    return std::nullopt;
}

}  // namespace goldfish::parsing
