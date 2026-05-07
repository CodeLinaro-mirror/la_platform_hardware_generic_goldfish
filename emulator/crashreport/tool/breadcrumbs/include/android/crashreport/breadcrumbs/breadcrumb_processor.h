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
#include <string>
#include <vector>

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"
#include "android/crashreport/breadcrumbs/trace_renderer_factory.h"

namespace android::crashreport::breadcrumbs {

/**
 * @brief The primary entry point for gRPC breadcrumb analysis.
 *
 * The BreadcrumbProcessor orchestrates the end-to-end pipeline:
 * 1. Binary Parsing (Parser)
 * 2. Semantic Resolution (Resolver)
 * 3. Temporal Aggregation (Aggregator)
 * 4. Visual Rendering (Renderer)
 */
class BreadcrumbProcessor {
  public:
    /**
     * @brief Processes a raw breadcrumb buffer and returns a rendered report.
     *
     * @param buffer Raw memory buffer from the minidump.
     * @param crashing_thread_id The ID of the thread where the crash occurred.
     * @param format The desired output format.
     * @return A formatted string containing the forensic report.
     */
    static std::string Process(
            const std::vector<uint8_t>& buffer, uint64_t crashing_thread_id,
            TraceRendererFactory::RenderFormat format = TraceRendererFactory::RenderFormat::kText,
            bool use_color = true);
};

}  // namespace android::crashreport::breadcrumbs
