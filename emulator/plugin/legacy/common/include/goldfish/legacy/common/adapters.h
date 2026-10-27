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

#include <functional>
#include <memory>

#include "goldfish/legacy/common/c/bar1_ops.h"
#include "goldfish/legacy/common/c/guest_memory_ops.h"
#include "goldfish/legacy/common/c/irq_ops.h"
#include "goldfish/legacy/common/c/mmio_core.h"
#include "goldfish/legacy/common/i_bar1_mapper.h"
#include "goldfish/legacy/common/i_guest_memory.h"
#include "goldfish/legacy/common/i_mmio_device.h"

namespace goldfish::legacy {

/**
 * @brief Wraps a C `GoldfishGuestMemoryOps` callback table in an `IGuestMemory` implementation.
 *
 * @param ops C guest-memory callback table supplied by the QOM shim.
 * @return Owned `IGuestMemory` adapter forwarding calls to `ops`.
 */
std::unique_ptr<IGuestMemory> WrapGuestMemory(const GoldfishGuestMemoryOps& ops);

using SetIrqCallback = std::function<void(bool)>;

/**
 * @brief Wraps a C `GoldfishIrqOps` callback table in a `SetIrqCallback` callback.
 *
 * @param ops C interrupt-line callback table supplied by the QOM shim.
 * @return Callback forwarding IRQ level changes to `ops`.
 */
SetIrqCallback WrapIrq(const GoldfishIrqOps& ops);

/**
 * @brief Wraps a C `GoldfishBar1Ops` callback table in an `IBar1Mapper` implementation.
 *
 * @param ops C BAR 1 mapping and BQL callback table supplied by the address-space QOM shim.
 * @return Owned `IBar1Mapper` adapter forwarding calls to `ops`.
 */
std::unique_ptr<IBar1Mapper> WrapBar1(const GoldfishBar1Ops& ops);

/**
 * @brief Transfers ownership of a C++ `IMmioDevice` into a C `GoldfishMmioCore` dispatch struct.
 *
 * @param device Owned C++ MMIO device core instance to expose to a C11 QOM shim.
 * @return Populated `GoldfishMmioCore` table owning the released `IMmioDevice` pointer.
 */
GoldfishMmioCore ToMmioCore(std::unique_ptr<IMmioDevice> device);

}  // namespace goldfish::legacy
