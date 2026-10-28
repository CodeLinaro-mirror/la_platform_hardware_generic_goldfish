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

#include "goldfish/legacy/common/c/bar1_ops.h"
#include "goldfish/legacy/common/c/guest_memory_ops.h"
#include "goldfish/legacy/common/c/irq_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Host environment callback bundle passed from a QOM shim to a `*CoreCreate` factory.
 */
struct GoldfishDeviceEnv {
    struct GoldfishGuestMemoryOps memory; /**< Guest physical RAM read/write accessors. */
    struct GoldfishIrqOps irq;            /**< Device IRQ line (SysBus GSI or PCI INTx pin B). */
    struct GoldfishBar1Ops bar1;          /**< Zeroed except for `goldfish_address_space`. */
};

#ifdef __cplusplus
}
#endif
