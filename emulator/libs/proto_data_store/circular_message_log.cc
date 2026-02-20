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

#include "goldfish/circular_message_log.h"

#include <cstdint>
#include <cstring>

#include "absl/status/status.h"
#include "google/protobuf/message.h"

namespace goldfish::proto_data_store {

absl::StatusOr<std::unique_ptr<CircularMessageLog>> CircularMessageLog::CreateWriter(
        void* buffer, size_t size, const google::protobuf::Message& prototype) {
    auto status = RawCircularLog::ValidateWriter(buffer, size);
    if (!status.ok()) return status;

    return std::make_unique<CircularMessageLog>(Private{}, buffer, size, true, prototype);
}

absl::StatusOr<std::unique_ptr<CircularMessageLog>> CircularMessageLog::CreateReader(
        void* buffer, size_t size, const google::protobuf::Message& prototype) {
    auto status = RawCircularLog::ValidateReader(buffer, size);
    if (!status.ok()) return status;

    return std::make_unique<CircularMessageLog>(Private{}, buffer, size, false, prototype);
}

CircularMessageLog::CircularMessageLog(Private, void* buffer, size_t size, bool should_init,
                                       const google::protobuf::Message& prototype)
        : engine_(RawCircularLog::Private{}, buffer, size, should_init), prototype_(prototype) {}

absl::Status CircularMessageLog::Push(const google::protobuf::Message& message) {
    const size_t payload_len = message.ByteSizeLong();
    return engine_.Push(static_cast<uint32_t>(payload_len), [&](void* data_ptr) {
        message.SerializeToArray(data_ptr, static_cast<int>(payload_len));
    });
}

void CircularMessageLog::ForEach(const Visitor& visitor) const {
    auto crumb = std::unique_ptr<google::protobuf::Message>(prototype_.New());
    engine_.ForEach([&](void* data, uint16_t size) -> bool {
        if (crumb->ParseFromArray(data, size)) {
            return visitor(*crumb);
        }
        return true;
    });
}

size_t CircularMessageLog::Capacity() const {
    return engine_.Capacity();
}

size_t CircularMessageLog::BytesUsed() const {
    return engine_.BytesUsed();
}

uint32_t CircularMessageLog::MessageCount() const {
    return engine_.ObjectCount();
}

}  // namespace goldfish::proto_data_store
