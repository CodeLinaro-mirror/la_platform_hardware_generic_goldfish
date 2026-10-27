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

#include <cstddef>
#include <cstdint>

namespace goldfish::legacy {

/**
 * @brief Safe guest physical RAM accessor used by C++ device cores to read/write command buffers.
 *
 * @note Must only be called on a QEMU vCPU or main-loop thread while holding the BQL. Guest
 *       physical addresses are untrusted; implementations reject unmapped ranges and integer
 *       overflows cleanly without aborting.
 */
class IGuestMemory {
  public:
    virtual ~IGuestMemory() = default;

    /**
     * @brief Copies `size` bytes from guest physical address `gpa` into host buffer `dst`.
     *
     * @param gpa Starting guest physical address.
     * @param dst Destination host buffer of at least `size` bytes.
     * @param size Number of bytes to read.
     * @return `true` if the entire `[gpa, gpa + size)` range resides in valid guest RAM and was
     *         copied; `false` on invalid GPA, unmapped hole, or overflow (`dst` is left untouched).
     */
    virtual bool Read(uint64_t gpa, void* dst, size_t size) = 0;

    /**
     * @brief Copies `size` bytes from host buffer `src` into guest physical address `gpa`.
     *
     * @param gpa Starting guest physical address.
     * @param src Source host buffer of at least `size` bytes.
     * @param size Number of bytes to write.
     * @return `true` if the entire `[gpa, gpa + size)` range was written to guest RAM; `false` if
     *         any portion is unmapped or overflows.
     */
    virtual bool Write(uint64_t gpa, const void* src, size_t size) = 0;
};

}  // namespace goldfish::legacy
