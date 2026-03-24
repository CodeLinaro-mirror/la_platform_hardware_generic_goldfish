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

#include "absl/log/check.h"
#include "absl/log/log.h"

#define MINI_CHROMIUM_BASE_LOGGING_H_
#define MINI_CHROMIUM_BASE_CHECK_H_

#include "client/annotation.h"

namespace android::crashreport {

using crashpad::Annotation;

/**
 * @brief A Crashpad annotation that reserves a raw binary buffer.
 *
 * Unlike SimpleStringAnnotation, this does not expect a string. It just reserves
 * memory and allows external components (like CircularMessageLog) to write
 * directly into it. The entire buffer is always captured in the minidump.
 *
 * @tparam MaxSize The size of the reserved buffer in bytes.
 */
template <Annotation::ValueSizeType MaxSize>
class BinaryAnnotation : public crashpad::Annotation {
  public:
    // Unique identifier for BinaryAnnotation type, must be above
    // crashpad::Annotation::kMaxValueSize to avoid conflicts with built-in types.
    static constexpr crashpad::Annotation::Type kBinary =
            static_cast<crashpad::Annotation::Type>(0x9001);
    BinaryAnnotation(const BinaryAnnotation&) = delete;
    BinaryAnnotation& operator=(const BinaryAnnotation&) = delete;

    /**
     * @brief Constructs a new binary annotation.
     *
     * @param name The name of the annotation as it will appear in the minidump.
     */
    explicit BinaryAnnotation(const char name[]) : Annotation(kBinary, name, buffer_), buffer_() {
        // We set the size to MaxSize immediately so the whole buffer is
        // considered "active" and will be included in crash reports.
        SetSize(MaxSize);
    }

    /** @brief Returns a pointer to the raw data buffer. */
    void* Data() { return buffer_; }

  private:
    uint8_t buffer_[MaxSize];
};

}  // namespace android::crashreport
