/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/parsing/utf8_iterator.h"

#include "utf8proc.h"

namespace goldfish::parsing {

Utf8Iterator::Utf8Iterator(const std::string_view source)
        : Utf8Iterator(reinterpret_cast<const uint8_t*>(source.data()), source.size()) {}

Utf8Iterator::Utf8Iterator(const uint8_t* begin, const size_t size)
        : Utf8Iterator(begin, begin + size) {}

Utf8Iterator::Utf8Iterator(const uint8_t* begin, const uint8_t* end) : begin_(begin), end_(end) {}

int32_t Utf8Iterator::operator()() {
    if (begin_ >= end_) {
        return -1;
    }

    utf8proc_int32_t codepoint;
    const utf8proc_ssize_t size = ::utf8proc_iterate(begin_, end_ - begin_, &codepoint);
    if (size > 0) {
        begin_ += size;
        return codepoint;
    } else {
        ++begin_;
        return 0xFFFD;  // REPLACEMENT CHARACTER
    }
}

}  // namespace goldfish::parsing
