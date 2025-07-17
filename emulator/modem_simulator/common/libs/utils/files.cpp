/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
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

bool FileExists(const std::string& path) {
    return std::filesystem::exists(path);
}

bool FileHasContent(const std::string& path) {
    return std::filesystem::file_size(path) > 0;
}

std::string AbsolutePath(const std::string& path) {
    try {
        return std::filesystem::absolute(path).string();
    } catch (const std::filesystem::filesystem_error&) {
        return std::string{};
    }
}

std::string ReadFile(const std::string& file) {
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return std::string{};
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    } catch (const std::exception&) {
        return std::string{};
    }
}

}  // namespace cuttlefish
