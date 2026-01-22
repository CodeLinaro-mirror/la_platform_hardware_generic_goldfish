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
#include "common/libs/utils/files.h"

#include <filesystem>

namespace cuttlefish {

bool FileExists(const std::string& path, bool follow_symlinks) {
    std::error_code ec;
    const bool r = std::filesystem::exists(path, ec);
    return !ec && r;
}

bool FileHasContent(const std::string& path) {
    std::error_code ec;
    const std::uintmax_t sz = std::filesystem::file_size(path, ec);
    return !ec && (sz > 0);
}

std::string AbsolutePath(const std::string& path) {
    std::error_code ec;
    auto ap = std::filesystem::absolute(path, ec);
    if (ec) {
        return {};
    } else {
        return ap.string();
    }
}

}  // namespace cuttlefish