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

#include "android/camera/QueryParser.h"

#include <cstring>

#include "goldfish/parsing/split2.h"

namespace goldfish::devices::camera {

using goldfish::parsing::split2;

void QueryParser::recv(const void* data0, const size_t size, const QueryParser::Sink& sink) {
    constexpr auto process = [](const char* begin, const char* end, const Sink& sink) {
        if (end > begin) {
            auto [query, params] = split2(std::string_view(begin, end - begin), ' ');
            sink(query, params);
        }
    };

    const char* data = static_cast<const char*>(data0);
    const char* const dataEnd = data + size;

    while (const char* const sep = static_cast<const char*>(::memchr(data, 0, dataEnd - data))) {
        if (mBuffer.empty()) {
            process(data, sep, sink);
        } else {
            const size_t querySize = mBuffer.size() + (sep - data);
            mBuffer.insert(mBuffer.end(), data, sep);
            process(mBuffer.data(), mBuffer.data() + querySize, sink);
            mBuffer.clear();
        }

        data = sep + 1;
    }

    mBuffer.insert(mBuffer.end(), data, dataEnd);
}

}  // namespace goldfish::devices::camera
