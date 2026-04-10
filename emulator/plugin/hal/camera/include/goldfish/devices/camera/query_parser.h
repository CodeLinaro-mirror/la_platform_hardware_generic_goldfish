/* Copyright 2025 The Android Open Source Project
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

#include <functional>
#include <string_view>
#include <vector>

namespace goldfish::devices::camera {

/*
 * QueryParser receives sequences of bytes and cuts them into queries using
 * the zero byte as a separator. A sequence of bytes received may contain an
 * incomplete query, multiple queries or any other combination of those.
 * A query is a pair of strings: name and parameters.
 */
struct QueryParser {
    using Sink = std::function<void(std::string_view, std::string_view)>;

    void recv(const void* data, size_t size, const Sink& sink);

  private:
    std::vector<char> mBuffer;
};

}  // namespace goldfish::devices::camera
