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

namespace goldfish::legacy {

/**
 * @brief Manages host-backed RAM subregions inside `goldfish_address_space` PCI BAR 1 and provides
 *        cross-thread BQL serialization.
 */
class IBar1Mapper {
  public:
    using OnRetiredCallback = std::function<void()>;

    virtual ~IBar1Mapper() = default;

    /**
     * @brief Returns the total byte capacity of the BAR 1 address window.
     *
     * @return Total BAR 1 size in bytes (typically 16 GiB on x86_64 or 8 GiB on Apple Silicon).
     */
    virtual uint64_t Size() const = 0;

    /**
     * @brief Returns the host OS page size required for all BAR 1 hypervisor mappings.
     *
     * @return Host page size in bytes (e.g., 4096 on x86_64, 16384 on Apple Silicon).
     */
    virtual uint64_t HostPageSize() const = 0;

    /**
     * @brief Returns the guest physical base address currently assigned to PCI BAR 1.
     *
     * @return Guest physical base address of BAR 1, or `0` if BAR 1 is not yet mapped by the guest.
     */
    virtual uint64_t GuestPhysBase() const = 0;

    /**
     * @brief Maps a host memory range into PCI BAR 1 at the given BAR-relative offset.
     *
     * @param offset Byte offset from the start of BAR 1; must be a multiple of `HostPageSize()`.
     * @param host_ptr Host virtual address to expose; must be aligned to `HostPageSize()`.
     * @param size Mapping length in bytes; must be non-zero and a multiple of `HostPageSize()`.
     * @return `true` if mapped successfully; `false` on misalignment, overlap, or out-of-bounds
     *         arguments (nothing is mapped on failure).
     */
    virtual bool Map(uint64_t offset, void* host_ptr, uint64_t size) = 0;

    /**
     * @brief Removes a previously mapped subregion starting at `offset` in BAR 1.
     *
     * @param offset Exact BAR-relative byte offset previously passed to `Map()`.
     */
    virtual void Unmap(uint64_t offset) = 0;

    /**
     * @brief Unmaps the subregion at `offset` and runs `on_retired` once RCU readers finish.
     *
     * @param offset Exact BAR-relative byte offset previously passed to `Map()`.
     * @param on_retired Optional callback invoked under the BQL once the mapping is fully retired.
     */
    virtual void UnmapAndRetire(uint64_t offset, OnRetiredCallback on_retired) {
        Unmap(offset);
        if (on_retired) {
            on_retired();
        }
    }

    /**
     * @brief Executes `fn` synchronously with the QEMU BQL held, acquiring it first if needed.
     *
     * @param fn Synchronous critical section to execute under the BQL.
     */
    virtual void RunUnderBql(const std::function<void()>& fn) { fn(); }
};

}  // namespace goldfish::legacy
