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

#include <algorithm>
#include <deque>
#include <iterator>
#include <memory>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "android/crashreport/breadcrumbs/breadcrumb_parser.h"
#include "android/crashreport/breadcrumbs/trace_aggregator.h"
#include "android/crashreport/breadcrumbs/trace_renderer_factory.h"
#include "emulator/crashreport/tool/breadcrumbs/breadcrumb_metadata.h"
#include "google_breakpad/processor/code_module.h"
#include "google_breakpad/processor/code_modules.h"
#include "google_breakpad/processor/source_line_resolver_interface.h"
#include "google_breakpad/processor/stack_frame.h"

namespace android::crashreport::breadcrumbs {

namespace {

/**
 * @brief Internal helper to resolve raw breadcrumbs using semantic metadata.
 */
std::vector<EnrichedBreadcrumb> ResolveEntries(
        const std::vector<Breadcrumb>& entries,
        google_breakpad::SourceLineResolverInterface* resolver,
        const google_breakpad::CodeModules* modules,
        const absl::flat_hash_map<uint32_t, std::string>& loop_names) {
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
            e.method_name = absl::StrFormat("%c%c%c%c", static_cast<char>(adb.command() & 0xFF),
                                            static_cast<char>((adb.command() >> 8) & 0xFF),
                                            static_cast<char>((adb.command() >> 16) & 0xFF),
                                            static_cast<char>((adb.command() >> 24) & 0xFF));

            const uint64_t flow_id = proto.flow_id();
            const uint32_t arg0 = static_cast<uint32_t>(flow_id >> 32);
            const uint32_t arg1 = static_cast<uint32_t>(flow_id & 0xFFFFFFFF);

            e.resolved_payload = absl::StrFormat(
                    "(%u, %u), ->%s", arg0, arg1,
                    adb.direction() == android::control::breadcrumbs::AdbPayload::TO_GUEST
                            ? "Guest"
                            : "Host");
            if (!adb.data_snippet().empty()) {
                e.resolved_payload += ", \"" + absl::CEscape(adb.data_snippet()) + "\"";
            }
        } else if (proto.has_looper()) {
            const auto& looper = proto.looper();
            if (auto it = loop_names.find(looper.loop_id()); it != loop_names.end()) {
                e.method_name = it->second;
            } else {
                e.method_name = absl::StrFormat("Looper-%u", looper.loop_id());
            }

            if (looper.event() == android::control::breadcrumbs::LooperPayload::POST) {
                uint64_t pc = looper.caller_pc();
                std::string location = absl::StrFormat("0x%llx", pc);

                if (resolver && modules) {
                    const google_breakpad::CodeModule* module = modules->GetModuleForAddress(pc);
                    if (module) {
                        google_breakpad::StackFrame frame;
                        frame.instruction = pc;
                        frame.module = module;
                        std::deque<std::unique_ptr<google_breakpad::StackFrame>> inlined_frames;
                        resolver->FillSourceLineInfo(&frame, &inlined_frames);

                        location = absl::StrFormat("%s (%s:%u)", frame.function_name,
                                                   frame.source_file_name, frame.source_line);
                    }
                }

                e.resolved_payload = location;
                if (!looper.context().empty()) {
                    absl::StrAppend(&e.resolved_payload, " [context: ", looper.context(), "]");
                }
            } else if (looper.event() == android::control::breadcrumbs::LooperPayload::EXECUTE) {
                e.resolved_payload = "";
            }
        } else {
            e.method_name = "UNKNOWN";
            e.resolved_payload = "Unknown payload type";
        }

        enriched.push_back(std::move(e));
    }

    return enriched;
}

}  // namespace

std::string BreadcrumbProcessor::Process(const std::vector<std::vector<uint8_t>>& buffers,
                                         uint64_t crashing_thread_id,
                                         TraceRendererFactory::RenderFormat format, bool use_color,
                                         const absl::flat_hash_map<uint64_t, uint64_t>& tid_map,
                                         google_breakpad::SourceLineResolverInterface* resolver,
                                         const google_breakpad::CodeModules* modules,
                                         std::string_view looper_registrations) {
    std::vector<Breadcrumb> all_entries;
    for (const auto& buffer : buffers) {
        auto entries = BreadcrumbParser::Parse(buffer);
        all_entries.insert(all_entries.end(), std::make_move_iterator(entries.begin()),
                           std::make_move_iterator(entries.end()));
    }

    if (all_entries.empty()) {
        return buffers.size() == 1 ? "No breadcrumbs found in buffer."
                                   : "No breadcrumbs found in buffers.";
    }

    // Sort by timestamp to interleave events correctly
    std::ranges::sort(all_entries, [](const Breadcrumb& a, const Breadcrumb& b) {
        return a.timestamp_ns() < b.timestamp_ns();
    });

    // Parse dynamic loop registration annotations
    absl::flat_hash_map<uint32_t, std::string> loop_names;
    if (!looper_registrations.empty()) {
        for (std::string_view entry :
             absl::StrSplit(looper_registrations, ';', absl::SkipEmpty())) {
            std::vector<std::string_view> kv = absl::StrSplit(entry, '=');
            if (kv.size() == 2) {
                uint32_t id;
                if (absl::SimpleAtoi(kv[0], &id)) {
                    loop_names[id] = std::string(kv[1]);
                }
            }
        }
    }

    // Enrich raw entries with semantic metadata, aggregate into a trace, and render
    auto enriched_entries = ResolveEntries(all_entries, resolver, modules, loop_names);

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
