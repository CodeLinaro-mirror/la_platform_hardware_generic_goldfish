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

#include "goldfish/parsing/split2.h"

#include "absl/strings/str_split.h"

namespace goldfish::parsing {

std::pair<std::string_view, std::string_view> Split2(const std::string_view str, const char sep) {
    return absl::StrSplit(str, absl::MaxSplits(sep, 1));
}

}  // namespace goldfish::parsing
