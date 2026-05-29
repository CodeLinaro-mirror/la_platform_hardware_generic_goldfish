// Copyright 2026 The Android Open Source Project
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
#include "android/crashreport/breadcrumbs/breadcrumb_parser.h"

#include "goldfish/circular_message_log.h"

namespace android::crashreport::breadcrumbs {

using goldfish::proto_data_store::CircularMessageLog;

std::vector<Breadcrumb> BreadcrumbParser::Parse(const std::vector<uint8_t>& buffer) {
    if (buffer.size() <= CircularMessageLog::kHeaderSize) return {};

    static const Breadcrumb kPrototype;

    // Create a reader to handle the wrap-around logic and committed-only filtering.
    auto log = CircularMessageLog::CreateReader(const_cast<uint8_t*>(buffer.data()), buffer.size(),
                                                kPrototype);
    if (!log.ok()) {
        return {};
    }

    std::vector<Breadcrumb> entries;
    entries.reserve((*log)->MessageCount());

    (*log)->ForEach([&](const google::protobuf::Message& msg) {
        entries.push_back(static_cast<const Breadcrumb&>(msg));
        return true;
    });

    return entries;
}

}  // namespace android::crashreport::breadcrumbs
