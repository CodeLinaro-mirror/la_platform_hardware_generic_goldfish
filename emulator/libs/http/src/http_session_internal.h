// Copyright (C) 2026 The Android Open Source Project
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

#include <llhttp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

#include "goldfish/http/http_request.h"

namespace goldfish::http::internal {

inline constexpr size_t kLlhttpMethodTableSize =
        static_cast<size_t>(std::max({HTTP_DELETE, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT,
                                      HTTP_OPTIONS, HTTP_PATCH})) +
        1;

inline constexpr auto kLlhttpMethodTable = []() {
    std::array<std::optional<HttpMethod>, kLlhttpMethodTableSize> table{};
    table[HTTP_DELETE] = HttpMethod::kDelete;
    table[HTTP_GET] = HttpMethod::kGet;
    table[HTTP_HEAD] = HttpMethod::kHead;
    table[HTTP_POST] = HttpMethod::kPost;
    table[HTTP_PUT] = HttpMethod::kPut;
    table[HTTP_OPTIONS] = HttpMethod::kOptions;
    table[HTTP_PATCH] = HttpMethod::kPatch;
    return table;
}();

// Maps a parsed `llhttp_method_t` enum value directly to an `HttpMethod`
// without string lookup, returning `std::nullopt` for unsupported or
// out-of-range method codes.
constexpr std::optional<HttpMethod> LlhttpMethodToHttpMethod(llhttp_method_t method) noexcept {
    const auto index = static_cast<size_t>(method);
    if (index >= kLlhttpMethodTable.size()) {
        return std::nullopt;
    }
    return kLlhttpMethodTable[index];
}

}  // namespace goldfish::http::internal
