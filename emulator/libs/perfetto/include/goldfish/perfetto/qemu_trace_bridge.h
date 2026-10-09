// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#if defined(CONFIG_TRACE_GOLDFISH)

#include <type_traits>

extern "C" {
#include "google/drivers/trace/goldfish.h"
}

namespace goldfish::perfetto {

/**
 * @brief Callback bridging QEMU's goldfish trace backend into Perfetto track events.
 *
 * Collects the `GOLDFISH_TRACE_EVENT_START`, `GOLDFISH_TRACE_ARG_*`, and
 * `GOLDFISH_TRACE_EVENT_END` sequence emitted by QEMU's tracetool:
 * - Events ending with `_begin` / `_end` (e.g., `aio_bh_call_begin` / `aio_bh_call_end`,
 *   `virtio_queue_handle_output_begin` / `virtio_queue_handle_output_end`) are paired
 *   into scoped Perfetto `TRACE_EVENT_BEGIN` / `TRACE_EVENT_END` slices on the `"qemu"`
 *   category with all trace arguments attached as debug annotations.
 * - Standalone point-in-time QEMU trace events (e.g., `virtqueue_pop`, `virtqueue_fill`)
 *   are emitted as `TRACE_EVENT_INSTANT` with all trace arguments attached.
 */
void OnQemuGoldfishTrace(const GoldfishTraceData* data);

}  // namespace goldfish::perfetto

#endif  // defined(CONFIG_TRACE_GOLDFISH)
