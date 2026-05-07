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
#include "android/crashreport/breadcrumbs/breadcrumb_processor.h"

#include <vector>

#include "android/crashreport/breadcrumbs/breadcrumb_parser.h"
#include "android/crashreport/breadcrumbs/trace_aggregator.h"
#include "android/crashreport/breadcrumbs/trace_renderer_factory.h"
#include "emulator/crashreport/tool/breadcrumbs/breadcrumb_metadata.h"

namespace android::crashreport::breadcrumbs {

namespace {

/**
 * @brief Internal helper to resolve raw breadcrumbs using semantic metadata.
 */
std::vector<EnrichedBreadcrumb> ResolveEntries(const std::vector<GrpcBreadcrumb>& entries) {
    std::vector<EnrichedBreadcrumb> enriched;
    enriched.reserve(entries.size());

    for (const auto& proto : entries) {
        EnrichedBreadcrumb e;
        e.proto = proto;

        const uint32_t hash = proto.method_hash();
        e.method_name = GetMethodName(hash);
        e.resolved_payload = ResolvePayload(hash, proto.payload(), proto.phase());

        enriched.push_back(std::move(e));
    }

    return enriched;
}

}  // namespace

std::string BreadcrumbProcessor::Process(const std::vector<uint8_t>& buffer,
                                         uint64_t crashing_thread_id,
                                         TraceRendererFactory::RenderFormat format,
                                         bool use_color) {
    // Extract raw breadcrumbs from the binary buffer
    auto raw_entries = BreadcrumbParser::Parse(buffer);
    if (raw_entries.empty()) {
        return "No gRPC breadcrumbs found in buffer.";
    }

    // Enrich raw entries with semantic metadata, aggregate into a trace, and render
    auto enriched_entries = ResolveEntries(raw_entries);
    auto trace = TraceAggregator::Aggregate(enriched_entries, crashing_thread_id);
    auto renderer = TraceRendererFactory::Create(format, use_color);
    return renderer->Render(trace);
}

}  // namespace android::crashreport::breadcrumbs
