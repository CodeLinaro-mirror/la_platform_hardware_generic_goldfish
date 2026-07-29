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
#include <cstring>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

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
 * The annotation is stored as a standard string annotation.
 * Format: "1=QemuLoop;2=LibuvLoop;..."
 *
 * @note Synchronization Design:
 * This class uses blocking synchronization (absl::Mutex) instead of lock-free
 * reservation or ScopedSpinGuard. Because SetSize() is only updated after
 * a write is fully completed, a crash during registration will only result in
 * losing the single in-flight registration. The previously registered loopers
 * will still be successfully captured. Using ScopedSpinGuard here would cause
 * the crash handler to discard the entire looper registrations registry if a
 * crash occurred during registration.
 */
template <crashpad::Annotation::ValueSizeType MaxSize>
struct LooperRegistrationsBufferStorage {
    std::array<char, MaxSize> buffer_{};
};

template <crashpad::Annotation::ValueSizeType MaxSize>
class LooperRegistrationsAnnotation : private LooperRegistrationsBufferStorage<MaxSize>,
                                      public crashpad::Annotation {
  public:
    LooperRegistrationsAnnotation(const char name[])
            : LooperRegistrationsBufferStorage<MaxSize>()
            , crashpad::Annotation(crashpad::Annotation::Type::kString, name,
                                   this->buffer_.data()) {
        SetSize(0);
    }

    void Append(uint8_t loop_id, std::string_view loop_name) {
        absl::MutexLock lock(mutex_);
        size_t current_size = size();

        // 3 chars max for uint8_t (max 255), 1 for '=', 1 for ';', 1 for '\0'
        constexpr size_t kMaxFormatOverhead = 6;
        if (current_size + loop_name.size() + kMaxFormatOverhead >= MaxSize) {
            LOG(WARNING) << "Diagnostic buffer limit reached. Crash diagnostics for event loop '"
                         << loop_name << "' will be skipped.";
            return;
        }

        int len = absl::SNPrintF(this->buffer_.data() + current_size, MaxSize - current_size,
                                 "%u=%s;", loop_id, loop_name);
        if (len > 0) {
            SetSize(current_size + len);
        }
    }

  private:
    absl::Mutex mutex_;
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
void RegisterLooper(uint8_t loop_id, std::string_view name);

}  // namespace android::crashreport
