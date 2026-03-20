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

#pragma once

#include <cstdint>
#include <string_view>

namespace goldfish::parsing {

struct Utf8Iterator {
    Utf8Iterator() = default;
    explicit Utf8Iterator(std::string_view source);
    Utf8Iterator(const uint8_t* begin, size_t size);
    Utf8Iterator(const uint8_t* begin, const uint8_t* end);

    int32_t operator()();

  private:
    const uint8_t* begin_ = nullptr;
    const uint8_t* end_ = nullptr;
};

}  // namespace goldfish::parsing
