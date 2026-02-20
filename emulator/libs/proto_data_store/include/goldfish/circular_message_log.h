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

#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "goldfish/raw_circular_log.h"

namespace google::protobuf {
class Message;
}

namespace goldfish::proto_data_store {

/**
 * @brief A high-performance, type-safe circular log for Protobuf messages.
 *
 * ProtoCircularLog provides a type-safe interface for storing and retrieving
 * Protobuf messages in a fixed-size memory region (e.g., shared memory).
 * It uses RawCircularLog as its underlying engine to handle eviction,
 * concurrency, and crash resilience.
 *
 * @section usage Usage Example
 * @code
 * // 1. Prepare a buffer (e.g., shared memory or stack)
 * std::vector<char> buffer(64 * 1024);
 *
 * // 2. Create a Writer (initializes the header)
 * auto log_status = ProtoCircularLog<MyProto>::CreateWriter(buffer.data(), buffer.size());
 * if (!log_status.ok()) return log_status.status();
 * auto& log = log_status.value();
 *
 * // 3. Push messages (automatically evicts oldest if full)
 * MyProto msg;
 * msg.set_id(123);
 * absl::Status status = log->Push(msg);
 *
 * // 4. Iterate over messages
 * log->ForEach([](const MyProto& m) {
 *     LOG(INFO) << "Seen message: " << m.id();
 *     return true; // Continue iteration
 * });
 * @endcode
 *
 * @tparam T The Protobuf message type.
 */
template <typename T>
class ProtoCircularLog {
  public:
    struct Private {
        explicit Private() = default;
    };
    explicit ProtoCircularLog(Private, void* buffer, size_t size, bool should_init)
            : engine_(RawCircularLog::Private{}, buffer, size, should_init) {}

    /**
     * @brief Callback invoked for each message during iteration.
     * @return true to continue, false to stop iteration.
     */
    using Visitor = std::function<bool(const T&)>;

    /**
     * @brief Creates a Writer instance that initializes a fresh log.
     * @param buffer Pointer to the start of the memory region.
     * @param size Total size of the region in bytes.
     */
    static absl::StatusOr<std::unique_ptr<ProtoCircularLog<T>>> CreateWriter(void* buffer,
                                                                             size_t size) {
        auto status = RawCircularLog::ValidateWriter(buffer, size);
        if (!status.ok()) return status;
        return std::make_unique<ProtoCircularLog<T>>(Private{}, buffer, size, true);
    }

    /**
     * @brief Creates a Reader instance that attaches to an existing log.
     * @param buffer Pointer to the start of the memory region.
     * @param size Total size of the region in bytes.
     */
    static absl::StatusOr<std::unique_ptr<ProtoCircularLog<T>>> CreateReader(void* buffer,
                                                                             size_t size) {
        auto status = RawCircularLog::ValidateReader(buffer, size);
        if (!status.ok()) return status;
        return std::make_unique<ProtoCircularLog<T>>(Private{}, buffer, size, false);
    }

    /**
     * @brief Appends a message to the log.
     *
     * This method handles Protobuf serialization and delegates to the
     * engine to manage the commit protocol.
     *
     * @param message The message to store.
     * @return OkStatus, or ResourceExhausted if the message is larger than Capacity.
     */
    absl::Status Push(const T& message) {
        const size_t payload_len = message.ByteSizeLong();
        return engine_.Push(static_cast<uint32_t>(payload_len), [&](void* data_ptr) {
            message.SerializeToArray(data_ptr, static_cast<int>(payload_len));
        });
    }

    /**
     * @brief Traverses valid messages from oldest to newest.
     *
     * Readers stop iterating when they encounter a record where `commit` is 0.
     *
     * @note This method holds the internal mutex for the entire duration of the traversal.
     * The visitor callback MUST NOT call any methods on this log (e.g., Push, MessageCount, etc.),
     * as it will result in a deadlock.
     *
     * @param visitor Callback invoked for each successfully committed message.
     */
    void ForEach(const Visitor& visitor) const {
        T crumb;
        engine_.ForEach([&](void* data, uint16_t size) -> bool {
            if (crumb.ParseFromArray(data, size)) {
                return visitor(crumb);
            }
            return true;
        });
    }

    /** @brief Total data capacity in bytes. */
    size_t Capacity() const { return engine_.Capacity(); }

    /** @brief Number of bytes currently utilized by committed messages. */
    size_t BytesUsed() const { return engine_.BytesUsed(); }

    /** @brief Total number of committed messages currently in the log. */
    uint32_t MessageCount() const { return engine_.ObjectCount(); }

    static constexpr size_t kHeaderSize = RawCircularLog::kHeaderSize;

  private:
    RawCircularLog engine_;
};

/**
 * @brief A type-erased circular log for Protobuf messages.
 *
 * This class is useful when the Protobuf type is only known at runtime
 * via a prototype message. For most use cases, ProtoCircularLog<T> is preferred.
 */
class CircularMessageLog {
  public:
    struct Private {
        explicit Private() = default;
    };
    CircularMessageLog(Private, void* buffer, size_t size, bool should_init,
                       const google::protobuf::Message& prototype);

    using Visitor = std::function<bool(const google::protobuf::Message&)>;

    static absl::StatusOr<std::unique_ptr<CircularMessageLog>> CreateWriter(
            void* buffer, size_t size, const google::protobuf::Message& prototype);

    static absl::StatusOr<std::unique_ptr<CircularMessageLog>> CreateReader(
            void* buffer, size_t size, const google::protobuf::Message& prototype);

    /** @brief Appends a message. */
    absl::Status Push(const google::protobuf::Message& message);

    /**
     * @brief Traverses valid messages from oldest to newest.
     *
     * Readers stop iterating when they encounter a record where `commit` is 0.
     *
     * @note This method holds the internal mutex for the entire duration of the traversal.
     * The visitor callback MUST NOT call any methods on this log (e.g., Push, MessageCount, etc.),
     * as it will result in a deadlock.
     *
     * @param visitor Callback invoked for each successfully committed message.
     */
    void ForEach(const Visitor& visitor) const;

    size_t Capacity() const;
    size_t BytesUsed() const;
    uint32_t MessageCount() const;

    static constexpr size_t kHeaderSize = RawCircularLog::kHeaderSize;

  private:
    RawCircularLog engine_;
    const google::protobuf::Message& prototype_;
};

}  // namespace goldfish::proto_data_store
