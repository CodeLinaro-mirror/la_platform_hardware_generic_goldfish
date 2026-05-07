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

#include <vector>

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"

namespace android::crashreport::breadcrumbs {

/**
 * @brief Logic for aggregating flat breadcrumb lists into a structured forensic model.
 *
 * The TraceAggregator performs the semantic grouping and temporal organization required
 * for multi-lane visualization. It handles thread-based lane creation and gRPC call
 * lifecycle tracking.
 */
class TraceAggregator {
  public:
    /**
     * @brief Transforms enriched breadcrumbs into a structured DiagnosticTrace.
     *
     * This is the primary entry point for model construction. It performs multiple passes
     * over the input events to discover threads, group call lifecycles, and assign
     * visual properties like color slots.
     *
     * @param events Pre-sorted list of breadcrumbs from the resolver.
     * @param crashing_thread_id The thread identified as the crash site.
     * @return A structured DiagnosticTrace model ready for rendering.
     * @see DiagnosticTrace
     */
    static DiagnosticTrace Aggregate(const std::vector<EnrichedBreadcrumb>& events,
                                     uint64_t crashing_thread_id);

  private:
    /**
     * @brief Assigns color slots to calls to ensure maximal visual contrast
     * between overlapping concurrent requests.
     *
     * Uses a sweep-line algorithm over call start/end boundaries to identify
     * concurrent active calls and assign them unique color slots from a reusable pool.
     *
     * @param trace The trace model to update with color assignments.
     */
    static void AssignColorSlots(DiagnosticTrace* trace);
};

}  // namespace android::crashreport::breadcrumbs
