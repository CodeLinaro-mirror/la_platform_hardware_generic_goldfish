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

#pragma once

#include <optional>
#include <string_view>

namespace goldfish::parsing {

/*
 * getKeyValueStr extracts the key value (specified by the `key` argument) from
 * the text (specified by the `text` argument), e.g.:
 *
 * getKeyValueStr("key=value", "key") will return "value",
 * getKeyValueStr("key=value", "not_a_key") will return std::nullopt.
 */
std::optional<std::string_view> getKeyValueStr(std::string_view text, std::string_view key);

}  // namespace goldfish::parsing
