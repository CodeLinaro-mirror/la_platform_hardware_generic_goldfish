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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PCI BAR 1 host-memory mapping and BQL synchronization operations for
 *        `goldfish_address_space`.
 *
 * BAR 1 is a 64-bit non-prefetchable container `MemoryRegion` (no backing host RAM allocated
 * upfront) into which host buffers (ASG rings, SharedSlots pools, and user-memory mappings) are
 * dynamically inserted as RAM subregions.
 */
struct GoldfishBar1Ops {
    void* opaque; /**< Shim-owned `goldfish_address_space` PCI device state. */

    /**
     * @brief Queries the guest physical base address assigned to BAR 1 by guest firmware/OS.
     *
     * Returns `0` if PCI BAR 1 is currently unmapped (`PCI_BAR_UNMAPPED`).
     */
    uint64_t (*guest_phys_base)(void* opaque);

    /**
     * @brief Maps `[host_ptr, host_ptr + size)` as a RAM subregion at BAR-relative `offset`.
     *
     * Acquires the BQL if not already held. Rejects zero sizes, out-of-bounds ranges, overlapping
     * subregions, or arguments not aligned to `host_page_size` (4 KiB on Linux/Windows x86_64,
     * 16 KiB on macOS Apple Silicon where unaligned hypervisor memslots abort the VM).
     */
    bool (*map)(void* opaque, uint64_t offset, void* host_ptr, uint64_t size);

    /**
     * @brief Removes a BAR 1 subregion previously created at `offset` via `map`.
     *
     * Equivalent to `unmap_and_retire(opaque, offset, NULL, NULL)`. Warning: backing host memory
     * must not be freed synchronously after `unmap` if vCPU RCU readers may still reference the old
     * `FlatView`; use `unmap_and_retire` when freeing backing memory.
     */
    void (*unmap)(void* opaque, uint64_t offset);

    /**
     * @brief Unmaps the subregion at `offset` and invokes `cb(cb_opaque)` after RCU retirement.
     *
     * Deletes the subregion from BAR 1 and drops the QOM reference on its `Bar1Subregion` owner.
     * Once all concurrent vCPU `FlatView` / `RAMBlock` RCU readers finish, the QOM finalizer runs
     * `cb(cb_opaque)` under the BQL so the caller can safely free the backing host memory.
     */
    void (*unmap_and_retire)(void* opaque, uint64_t offset, void (*cb)(void*), void* cb_opaque);

    /**
     * @brief Executes `fn(fn_opaque)` synchronously with the QEMU BQL held.
     *
     * Acquires the BQL first if the calling thread does not already hold it. Callable from any
     * thread, including `gfxstream` decoder threads handling `map_user_memory` /
     * `unmap_user_memory`.
     *
     * @warning Implementations MUST NOT dereference `opaque`, because a `gfxstream` background
     *          thread may invoke `run_locked` after the PCI device has been unrealized. Lock order
     *          is strictly `BQL -> device locks`.
     */
    void (*run_locked)(void* opaque, void (*fn)(void*), void* fn_opaque);

    uint64_t size; /**< Total BAR 1 window byte size (16 GiB on x86_64; 8 GiB on Apple Silicon). */
    uint64_t host_page_size; /**< Host OS page size (`GoldfishAddressSpaceHostPageSize()`); all
                                  `map` offsets, pointers, and sizes must be multiples of this
                                  value. */
};

#ifdef __cplusplus
}
#endif
