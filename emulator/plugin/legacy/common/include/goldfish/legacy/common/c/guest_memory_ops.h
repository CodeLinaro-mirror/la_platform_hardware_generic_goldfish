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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Guest physical memory read/write operations provided by a C11 QOM shim.
 *
 * Implemented in production shims via `address_space_map` and `address_space_unmap` looping across
 * page and RAM-block boundaries. Callers must hold the QEMU Big QEMU Lock (BQL).
 */
struct GoldfishGuestMemoryOps {
    void* opaque; /**< Shim-owned context pointer passed to `read` and `write`. */

    /**
     * @brief Copies `size` bytes from guest physical address `gpa` into `dst`.
     *
     * Staged internally so that if any byte in `[gpa, gpa + size)` does not map to valid guest RAM
     * (or `gpa + size` overflows), `false` is returned without modifying `dst`. Zero-length reads
     * succeed immediately.
     */
    bool (*read)(void* opaque, uint64_t gpa, void* dst, size_t size);

    /**
     * @brief Copies `size` bytes from `src` into guest physical address `gpa`.
     *
     * Returns `false` if `gpa + size` overflows or if any sub-range fails to map as writable guest
     * RAM. Zero-length writes succeed immediately.
     */
    bool (*write)(void* opaque, uint64_t gpa, const void* src, size_t size);
};

#ifdef __cplusplus
}
#endif
