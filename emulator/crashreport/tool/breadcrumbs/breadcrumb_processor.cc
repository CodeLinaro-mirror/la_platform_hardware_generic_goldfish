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
std::vector<EnrichedBreadcrumb> ResolveEntries(const std::vector<Breadcrumb>& entries) {
    std::vector<EnrichedBreadcrumb> enriched;
    enriched.reserve(entries.size());

    for (const auto& proto : entries) {
        EnrichedBreadcrumb e;
        e.proto = proto;

        if (proto.has_grpc()) {
            const auto& grpc = proto.grpc();
            const uint32_t hash = grpc.method_hash();
            e.method_name = GetMethodName(hash);
            if (e.method_name == "unknown_method") {
                e.method_name = absl::StrFormat("unknown_method (0x%08x)", hash);
            }
            // Cast grpc_phase to GrpcBreadcrumb::Phase as expected by generated ResolvePayload
            e.resolved_payload = ResolvePayload(
                    hash, grpc.payload(),
                    static_cast<android::control::interceptor::GrpcBreadcrumb::Phase>(
                            grpc.grpc_phase()));
        } else if (proto.has_adb()) {
            const auto& adb = proto.adb();
            e.method_name = "ADB";
            // ADB commands are packed 4-character codes in a 32-bit integer (little-endian).
            // We extract each byte and cast to char to reconstruct the string (e.g., "CNXN").
            e.resolved_payload = absl::StrFormat(
                    "CMD: %c%c%c%c, Dir: %s", static_cast<char>(adb.command() & 0xFF),
                    static_cast<char>((adb.command() >> 8) & 0xFF),
                    static_cast<char>((adb.command() >> 16) & 0xFF),
                    static_cast<char>((adb.command() >> 24) & 0xFF),
                    adb.direction() == android::control::breadcrumbs::AdbPayload::TO_GUEST
                            ? "TO_GUEST"
                            : "TO_HOST");
        } else {
            e.method_name = "UNKNOWN";
            e.resolved_payload = "Unknown payload type";
        }

        enriched.push_back(std::move(e));
    }

    return enriched;
}

}  // namespace

std::string BreadcrumbProcessor::Process(const std::vector<uint8_t>& buffer,
                                         uint64_t crashing_thread_id,
                                         TraceRendererFactory::RenderFormat format, bool use_color,
                                         const absl::flat_hash_map<uint64_t, uint64_t>& tid_map) {
    // Extract raw breadcrumbs from the binary buffer
    auto raw_entries = BreadcrumbParser::Parse(buffer);
    if (raw_entries.empty()) {
        return "No breadcrumbs found in buffer.";
    }

    // Enrich raw entries with semantic metadata, aggregate into a trace, and render
    auto enriched_entries = ResolveEntries(raw_entries);

    // Translate thread IDs if mapping is provided
    if (!tid_map.empty()) {
        for (auto& e : enriched_entries) {
            auto it = tid_map.find(e.proto.thread_id());
            if (it != tid_map.end()) {
                e.proto.set_thread_id(it->second);
            }
        }
    }

    auto trace = TraceAggregator::Aggregate(enriched_entries, crashing_thread_id);
    auto renderer = TraceRendererFactory::Create(format, use_color);
    return renderer->Render(trace);
}

}  // namespace android::crashreport::breadcrumbs
