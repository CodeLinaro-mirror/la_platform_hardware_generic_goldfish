/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "files.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace cuttlefish {

namespace fs = std::filesystem;

bool FileExists(const std::string& path) {
    return fs::exists(path);
}

bool FileHasContent(const std::string& path) {
    // Check if the file exists and its size is greater than 0.
    // fs::file_size will throw an error if the path doesn't exist or isn't a regular file.
    // Using fs::is_empty directly is also an option for files.
    std::error_code ec;  // For no-throw overload of file_size
    uintmax_t size = fs::file_size(path, ec);
    return !ec && size > 0;
}

std::string AbsolutePath(const std::string& path) {
    try {
        return fs::absolute(path).string();
    } catch (const fs::filesystem_error&) {
        return {};  // Return empty string on error
    }
}

off_t FileSize(const std::string& path) {
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        return -1;  // Indicate error (e.g., file doesn't exist, not a regular file)
    }
    return static_cast<off_t>(size);
}

std::string ReadFile(const std::string& file_path) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::stringstream buffer;
    try {
        buffer << in.rdbuf();
    } catch (const std::exception&) {
        return {};
    }
    return buffer.str();
}

}  // namespace cuttlefish