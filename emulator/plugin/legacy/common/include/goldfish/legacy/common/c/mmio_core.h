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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Type-erased C handle through which a QOM shim drives its C++ `IMmioDevice` core.
 *
 * Every callback in this table is invoked on a QEMU thread while holding the BQL.
 */
struct GoldfishMmioCore {
    void* core; /**< Owning pointer to the underlying C++ `IMmioDevice` instance. */

    /** @brief Handles a 4-byte-aligned 32-bit MMIO register read at `offset`. */
    uint32_t (*read)(void* core, uint32_t offset);

    /** @brief Handles a 4-byte-aligned 32-bit MMIO register write of `value` at `offset`. */
    void (*write)(void* core, uint32_t offset, uint32_t value);

    /** @brief Resets all guest-visible protocol state during device or machine reset. */
    void (*reset)(void* core);

    /** @brief Destroys `core` and releases owned resources during QOM unrealize/exit. */
    void (*destroy)(void* core);
};

#ifdef __cplusplus
}
#endif
