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

#include <cstdint>
#include <functional>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::proto_data_store {

/**
 * @brief A low-level, byte-agnostic circular log engine.
 *
 * RawCircularLog manages the memory layout, eviction logic, and crash-resilience
 * headers for a circular buffer of variable-length objects. It is designed
 * to be used in shared memory where one process writes and another may read,
 * even after a crash.
 *
 * @section protocol Commit Protocol
 * To ensure readers never see partially written data, the engine implements
 * a two-phase commit for each object:
 * 1. Reserve space and evict old objects (updates head/tail).
 * 2. Write an ObjectHeader with `commit = 0`.
 * 3. Invoke the Serializer callback to write the payload.
 * 4. Write the ObjectHeader with `commit = 1`.
 *
 * Readers (ForEach) stop iterating when they encounter a record where `commit` is 0.
 */
class RawCircularLog {
  public:
    struct Private {
        explicit Private() = default;
    };
    RawCircularLog(Private, void* buffer, size_t size, bool should_initialize);

    /**
     * @brief Prefix for every object in the data region.
     */
    union ObjectHeader {
        struct {
            uint16_t commit : 1;  ///< 1 if successfully written, 0 otherwise.
            uint16_t size : 15;   ///< Size of the payload in bytes.
        } fields;
        uint16_t raw;
    } __attribute__((packed));

    /**
     * @brief Callback for raw byte iteration.
     * @return true to continue, false to stop.
     */
    using RawVisitor = std::function<bool(void* data, uint16_t size)>;

    /**
     * @brief Callback for serializing data directly into the log.
     */
    using Serializer = std::function<void(void* dest)>;

    /**
     * @brief Creates a Writer instance that initializes a fresh log.
     * @param buffer Pointer to the start of the memory region.
     * @param size Total size of the region in bytes.
     */
    static absl::StatusOr<std::unique_ptr<RawCircularLog>> CreateWriter(void* buffer, size_t size);

    /**
     * @brief Creates a Reader instance that attaches to an existing log.
     * @param buffer Pointer to the start of the memory region.
     * @param size Total size of the region in bytes.
     */
    static absl::StatusOr<std::unique_ptr<RawCircularLog>> CreateReader(void* buffer, size_t size);

    /**
     * @brief Validates if the buffer and size are suitable for a Writer.
     */
    static absl::Status ValidateWriter(void* buffer, size_t size);

    /**
     * @brief Validates if the buffer contains a valid log for a Reader.
     */
    static absl::Status ValidateReader(void* buffer, size_t size);

    /**
     * @brief Appends an object to the log.
     *
     * @param payload_size Size of the data to be written by the serializer.
     * @param serializer Callback that writes exactly payload_size bytes to dest.
     * @return OkStatus on success, ResourceExhausted if payload_size is too large.
     */
    absl::Status Push(uint32_t payload_size, const Serializer& serializer);

    /** @brief Traverses all committed objects from oldest to newest.
     *
     * @note This method holds the internal mutex for the entire duration of the traversal.
     * The visitor callback MUST NOT call any methods on this log (e.g., Push, ObjectCount, etc.)
     * as it will result in a deadlock. The visitor should be efficient and non-blocking.
     */
    void ForEach(const RawVisitor& visitor) const;

    /** @brief Total data capacity in bytes (excluding headers). */
    size_t Capacity() const;

    /** @brief Number of bytes currently used by committed payloads. */
    size_t BytesUsed() const;

    /** @brief Number of committed objects currently in the log. */
    uint32_t ObjectCount() const {
        const absl::MutexLock lock(mutex_);
        return *count_ptr_;
    }

    static constexpr uint32_t kMagic = 0x41434C31;  // 'ACL1'
    static constexpr size_t kHeaderSize = 16;       // magic(4) + head(4) + tail(4) + count(4)
    static constexpr uint16_t kMaxObjectSize = 0x7FFF;

  private:
    struct RawHeader {
        uint32_t magic;
        uint32_t head;
        uint32_t tail;
        uint32_t count;
    };
    static_assert(sizeof(RawHeader) == kHeaderSize,
                  "RawHeader size mismatch. Check for unexpected padding.");
    static_assert(sizeof(ObjectHeader) == 2, "ObjectHeader must be exactly 2 bytes.");

    void InitializeHeader();
    bool IsEmpty() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { return *count_ptr_ == 0; }

    absl::StatusOr<uint32_t> Reserve(uint32_t payload_size) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

    void* GetPointer(uint32_t offset) { return buffer_ + kHeaderSize + offset; }

    static constexpr size_t kAlignment = sizeof(void*);

    // Round down size to the previous multiple of kAlignment by masking out the lower bits.
    static constexpr size_t RoundDown(size_t size) { return size & ~(kAlignment - 1); }

    // Round up size to the next multiple of kAlignment
    static constexpr size_t Align(size_t size) { return RoundDown(size + kAlignment - 1); }

    // Align pointer to the next multiple of kAlignment
    static void* AlignPointer(void* ptr) {
        static_assert(sizeof(size_t) >= sizeof(uintptr_t),
                      "size_t must be large enough to hold a uintptr_t");
        return reinterpret_cast<void*>(Align(reinterpret_cast<uintptr_t>(ptr)));
    }

    static void SafeAtomicWrite(void* buffer, ObjectHeader header);

    char* buffer_;
    size_t total_size_;
    size_t data_capacity_;

    uint32_t* head_ptr_;
    uint32_t* tail_ptr_;
    uint32_t* count_ptr_;

    mutable absl::Mutex mutex_;
};

}  // namespace goldfish::proto_data_store
