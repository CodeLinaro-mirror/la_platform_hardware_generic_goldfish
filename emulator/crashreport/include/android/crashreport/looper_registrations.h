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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

// Override mini-chromium headers to prevent macro conflicts (e.g. LOG, CHECK)
// between Crashpad's internal utilities and Abseil logging.
#define MINI_CHROMIUM_BASE_LOGGING_H_
#define MINI_CHROMIUM_BASE_CHECK_H_

#include "client/annotation.h"

namespace android::crashreport {

/**
 * @brief Thread-safe Crashpad annotation for gathering looper name registries.
 *
 * This allows event loops to dynamically register their names on startup.
 * The annotation is stored as a standard string annotation formatted as
 * "1=QemuLoop;2=LibuvLoop;...".
 *
 * @note Architecture & Invariants:
 * - Architectural Role: Maintains the centralized Crashpad annotation string mapping
 *   numeric loop IDs to human-readable loop names for minidump post-processing.
 * - Deduplication & Zero Heap Overhead: Distinct loop names are tracked in an internal
 *   hash map (registered_loopers_) keyed by std::string_view instances that point directly
 *   into the underlying annotation buffer (buffer_). Lookups and duplicate registrations
 *   execute in O(1) time without heap allocations or buffer re-formatting.
 * - Thread Safety: All operations are synchronized via registrations_mutex_.
 * - Invariant: size() <= MaxSize holds whenever registrations_mutex_ is not held.
 * - Lifetime: Keys in registered_loopers_ borrow persistent memory from buffer_, ensuring
 *   they remain valid for the lifetime of this annotation instance.
 */
template <crashpad::Annotation::ValueSizeType MaxSize>
struct LooperRegistrationsBufferStorage {
    std::array<char, MaxSize> buffer_{};
};

template <crashpad::Annotation::ValueSizeType MaxSize>
class LooperRegistrationsAnnotation : private LooperRegistrationsBufferStorage<MaxSize>,
                                      public crashpad::Annotation {
  public:
    explicit LooperRegistrationsAnnotation(const char name[])
            : LooperRegistrationsBufferStorage<MaxSize>()
            , crashpad::Annotation(crashpad::Annotation::Type::kString, name,
                                   this->buffer_.data()) {
        SetSize(0);
    }

    /**
     * @brief Gets the existing loop_id for loop_name or registers a new one sequentially.
     *
     * If loop_name has already been registered, returns its existing loop_id.
     * Otherwise, assigns the next sequential loop_id, appends it to the Crashpad
     * annotation buffer, records it in the lookup map, and returns the assigned ID.
     *
     * @param loop_name The name of the event loop.
     * @return The unique numeric identifier assigned to this event loop name.
     */
    uint8_t GetOrRegister(std::string_view loop_name) {
        absl::MutexLock registrations_lock(&registrations_mutex_);
        auto it = registered_loopers_.find(loop_name);
        if (it != registered_loopers_.end()) {
            return it->second;
        }

        uint8_t loop_id = next_loop_id_;
        if (next_loop_id_ < std::numeric_limits<uint8_t>::max()) {
            next_loop_id_++;
        }

        size_t current_size = size();

        // 3 chars max for uint8_t (max 255), 1 for '=', 1 for ';', 1 for '\0'
        constexpr size_t kMaxFormatOverhead = 6;
        if (current_size + loop_name.size() + kMaxFormatOverhead >= MaxSize) {
            LOG(WARNING) << "Diagnostic buffer limit reached. Crash diagnostics for event loop '"
                         << loop_name << "' will be skipped.";
            return loop_id;
        }

        int len = absl::SNPrintF(this->buffer_.data() + current_size, MaxSize - current_size,
                                 "%u=%s;", loop_id, loop_name);
        if (len <= 0) {
            return loop_id;
        }

        char* entry_start = this->buffer_.data() + current_size;
        char* eq_ptr = static_cast<char*>(std::memchr(entry_start, '=', len));
        DCHECK(eq_ptr != nullptr);
        char* name_start = eq_ptr + 1;

        std::string_view persistent_name(name_start, loop_name.size());
        registered_loopers_.emplace(persistent_name, loop_id);

        SetSize(current_size + len);
        return loop_id;
    }

  private:
    mutable absl::Mutex registrations_mutex_;
    uint8_t next_loop_id_ ABSL_GUARDED_BY(registrations_mutex_) = 1;
    absl::flat_hash_map<std::string_view, uint8_t> registered_loopers_
            ABSL_GUARDED_BY(registrations_mutex_);
};

template <crashpad::Annotation::ValueSizeType MaxSize>
struct DynamicBinaryStorage {
    std::string name_str;
    std::array<uint8_t, MaxSize> buffer_{};
    DynamicBinaryStorage(std::string name_val) : name_str(std::move(name_val)) {}
};

// DynamicBinaryAnnotation uses private inheritance from DynamicBinaryStorage to guarantee
// that name_str and buffer_ are fully constructed before crashpad::Annotation's constructor
// runs.
// WARNING: DynamicBinaryStorage MUST be declared BEFORE crashpad::Annotation in the base class
// list to ensure correct initialization order. Changing the order will cause undefined behavior.
template <crashpad::Annotation::ValueSizeType MaxSize>
class DynamicBinaryAnnotation : private DynamicBinaryStorage<MaxSize>, public crashpad::Annotation {
  public:
    static constexpr crashpad::Annotation::Type kBinary =
            static_cast<crashpad::Annotation::Type>(0x9001);

    DynamicBinaryAnnotation(std::string_view name_val)
            : DynamicBinaryStorage<MaxSize>(absl::StrCat("event_", name_val))
            , crashpad::Annotation(kBinary, this->name_str.c_str(), this->buffer_.data()) {
        CHECK_LT(this->name_str.size(), crashpad::Annotation::kNameMaxLength)
                << "Event name '" << this->name_str << "' is too long for Crashpad annotation";
        SetSize(Capacity());
    }

    std::span<uint8_t> Data() { return std::span<uint8_t>(this->buffer_.data(), size()); }

    constexpr size_t Capacity() const { return MaxSize; }

  private:
    static_assert(MaxSize <= crashpad::Annotation::kValueMaxSize,
                  "Binary data exceeds annotation size");
};

// Global function to register a looper.
uint8_t RegisterLooper(std::string_view name);

}  // namespace android::crashreport
