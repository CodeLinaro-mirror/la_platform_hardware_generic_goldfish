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

namespace goldfish::legacy {

/**
 * @brief Base interface implemented by all QEMU-header-free C++20 legacy MMIO device cores.
 *
 * Every method is invoked by the C11 QOM shim (`GoldfishMmioCore`) on a QEMU vCPU or main-loop
 * thread with the BQL held.
 */
class IMmioDevice {
  public:
    virtual ~IMmioDevice() = default;

    /**
     * @brief Reads a 32-bit device register at a 4-byte-aligned MMIO `offset`.
     *
     * @param offset Byte offset within the device's MMIO BAR/region (`offset % 4 == 0`).
     * @return 32-bit little-endian register value returned to the guest.
     */
    virtual uint32_t Read(uint32_t offset) = 0;

    /**
     * @brief Writes a 32-bit `value` to a device register at a 4-byte-aligned MMIO `offset`.
     *
     * @param offset Byte offset within the device's MMIO BAR/region (`offset % 4 == 0`).
     * @param value 32-bit little-endian value written by the guest.
     */
    virtual void Write(uint32_t offset, uint32_t value) = 0;

    /**
     * @brief Resets all guest-visible registers, handles, and active resources to initial state.
     */
    virtual void Reset() = 0;
};

}  // namespace goldfish::legacy
