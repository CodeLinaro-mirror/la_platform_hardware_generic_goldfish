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

#include "goldfish/raw_circular_log.h"

#include <cstdint>
#include <cstring>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

namespace goldfish::proto_data_store {

void RawCircularLog::SafeAtomicWrite(void* buffer, ObjectHeader header) {
    static_assert(sizeof(header) == 2,
                  "The circular_message_log is expecting 2 byte header lengths.");
    if (reinterpret_cast<uintptr_t>(buffer) % sizeof(header) == 0) {
        // Safe path: Aligned 2-byte write can be atomic.
        auto value = *reinterpret_cast<uint16_t*>(&header);
        auto* ptr = reinterpret_cast<uint16_t*>(buffer);
        __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
    } else {
        // Unaligned write: Use memcpy to avoid potential tearing.
        // if you are unlucky to crash when this happens you could end up
        // with a broken header.
        std::memcpy(buffer, &header, sizeof(header));
    }
}

absl::Status RawCircularLog::ValidateWriter(void* buffer, size_t size) {
    if (!buffer) return absl::InvalidArgumentError("Buffer pointer is null.");
    if (size <= kHeaderSize) {
        return absl::InvalidArgumentError("Buffer size is too small for header.");
    }
    return absl::OkStatus();
}

absl::Status RawCircularLog::ValidateReader(void* buffer, size_t size) {
    if (!buffer) return absl::InvalidArgumentError("Buffer pointer is null.");
    if (size <= kHeaderSize) {
        return absl::InvalidArgumentError("Buffer size is too small for header.");
    }
    auto* h = static_cast<RawHeader*>(buffer);
    if (h->magic != kMagic) {
        return absl::NotFoundError(
                absl::StrFormat("Cannot find magic header, %d != %d", h->magic, kMagic));
    }
    return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<RawCircularLog>> RawCircularLog::CreateWriter(void* buffer,
                                                                             size_t size) {
    auto status = ValidateWriter(buffer, size);
    if (!status.ok()) return status;

    return std::make_unique<RawCircularLog>(Private{}, buffer, size, true);
}

absl::StatusOr<std::unique_ptr<RawCircularLog>> RawCircularLog::CreateReader(void* buffer,
                                                                             size_t size) {
    auto status = ValidateReader(buffer, size);
    if (!status.ok()) return status;

    return std::make_unique<RawCircularLog>(Private{}, buffer, size, false);
}

RawCircularLog::RawCircularLog(Private, void* buffer, size_t size, bool should_initialize)
        : buffer_(static_cast<char*>(buffer))
        , total_size_(size)
        , data_capacity_(size > kHeaderSize ? size - kHeaderSize : 0) {
    auto* h = static_cast<RawHeader*>(buffer);
    head_ptr_ = &h->head;
    tail_ptr_ = &h->tail;
    count_ptr_ = &h->count;

    if (should_initialize) {
        InitializeHeader();
    }
}

void RawCircularLog::InitializeHeader() {
    auto* h = reinterpret_cast<RawHeader*>(buffer_);
    h->magic = kMagic;
    *head_ptr_ = 0;
    *tail_ptr_ = 0;
    *count_ptr_ = 0;
    if (total_size_ > kHeaderSize) {
        std::memset(buffer_ + kHeaderSize, 0, data_capacity_);
    }
}

absl::Status RawCircularLog::Push(uint32_t payload_size, const Serializer& serializer) {
    if (payload_size > kMaxObjectSize) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Payload size %d exceeds maximum allowed %d", payload_size, kMaxObjectSize));
    }

    const absl::MutexLock lock(mutex_);
    auto offset_status = Reserve(payload_size);
    if (!offset_status.ok()) return offset_status.status();

    void* data_ptr = GetPointer(offset_status.value());

    // Write uncommitted header
    SafeAtomicWrite(data_ptr, {.commit = 0, .size = 0});

    // Invoke serializer
    serializer(static_cast<char*>(data_ptr) + sizeof(ObjectHeader));

    // Atomically commit
    const ObjectHeader header_val = {.commit = 1, .size = static_cast<uint16_t>(payload_size)};
    SafeAtomicWrite(data_ptr, header_val);

    return absl::OkStatus();
}

absl::StatusOr<uint32_t> RawCircularLog::Reserve(uint32_t payload_size) {
    const uint32_t required = payload_size + sizeof(ObjectHeader);
    if (required > data_capacity_) {
        return absl::ResourceExhaustedError(absl::StrFormat(
                "Payload size %d exceeds data capacity %d", payload_size, data_capacity_));
    }

    const uint32_t h_old = *head_ptr_;
    const uint32_t t_old = *tail_ptr_;

    // Calculate the geometry of the new reservation.
    // We define two "Danger Zones" that must not contain the start of any valid message:
    //
    // Case 1: Simple Append (No Wrap)
    // [ T . . . . . H [ NEW ] . . . ]  -> Zone A: [H, H_NEW)
    //
    // Case 2: Wrap Around
    // [ [ NEW ] . . T . . . . H [ G ] ] -> Zone A: [0, H_NEW), Zone B: [H, CAPACITY)
    //
    // The while loop below advances the Tail until it no longer points into either zone.
    const bool wrapping = (h_old + required > data_capacity_);
    const uint32_t reserved_start = wrapping ? 0 : h_old;
    const uint32_t h_new = reserved_start + required;

    if (wrapping && data_capacity_ > h_old) {
        std::memset(buffer_ + kHeaderSize + h_old, 0, data_capacity_ - h_old);
    }

    *head_ptr_ = h_new;
    if (h_old == 0 && t_old == 0) {
        *count_ptr_ = 1;
        return reserved_start;
    }

    // 4. Advance the Tail to maintain the invariant.
    //
    // INVARIANT: The Tail must never point to memory reclaimed by the Head.
    // This happens if the Tail is overtaken by the New Message (Zone A) or
    // falls into the End-of-Tape Gap (Zone B).
    auto is_in_danger_zone = [&](uint32_t pos) {
        if (pos >= reserved_start && pos < h_new) return true;              // Zone A
        if (wrapping && pos >= h_old && pos < data_capacity_) return true;  // Zone B
        return false;
    };

    uint32_t t = t_old;
    while (is_in_danger_zone(t)) {
        ObjectHeader header;
        std::memcpy(&header, buffer_ + kHeaderSize + t, sizeof(header));

        if (header.commit && header.size > 0) {
            // ADVANCEMENT: If pointing to a valid message, move to the next header.
            const uint32_t msg_end = t + sizeof(ObjectHeader) + header.size;
            t = msg_end;
            if (t >= data_capacity_) t = 0;
            (*count_ptr_)--;
        } else {
            // TELEPORT: If pointing to an invalid/gap region, jump to the next valid segment:
            if (wrapping && t >= h_old) {
                t = 0;  // From the End-Gap -> Offset 0.
            } else {
                t = h_new;  // From the New-Message -> New Head.
            }
        }

        if (t == t_old) {
            t = h_new;
            break;
        }
    }
    *tail_ptr_ = t;
    (*count_ptr_)++;

    return reserved_start;
}

void RawCircularLog::ForEach(const RawVisitor& visitor) const {
    const absl::MutexLock lock(mutex_);
    const uint32_t h = *head_ptr_;
    const uint32_t t = *tail_ptr_;

    if (IsEmpty()) return;

    auto loop = [&](uint32_t start, uint32_t end) -> bool {
        uint32_t pos = start;
        while (pos + sizeof(ObjectHeader) <= end) {
            ObjectHeader header;
            std::memcpy(&header, buffer_ + kHeaderSize + pos, sizeof(header));
            if (!header.commit) break;  // Bad record, stop iteration.
            if (pos + sizeof(ObjectHeader) + header.size > end) {
                break;  // Incomplete record, stop iteration.
            }
            if (!visitor(buffer_ + kHeaderSize + pos + sizeof(ObjectHeader), header.size)) {
                return false;
            }
            pos += sizeof(ObjectHeader) + header.size;
        }
        return true;
    };

    if (t < h) {
        loop(t, h);
    } else {
        // Wrapped case: iterate from Tail -> End, then from Start -> Head.
        if (loop(t, static_cast<uint32_t>(data_capacity_))) {
            loop(0, h);
        }
    }
}

size_t RawCircularLog::Capacity() const {
    return data_capacity_;
}

size_t RawCircularLog::BytesUsed() const {
    const absl::MutexLock lock(mutex_);
    const uint32_t h = *head_ptr_;
    const uint32_t t = *tail_ptr_;
    if (IsEmpty()) return 0;
    if (h > t) return h - t;
    return data_capacity_ - t + h;
}

}  // namespace goldfish::proto_data_store
