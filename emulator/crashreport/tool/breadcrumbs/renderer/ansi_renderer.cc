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
#include "android/crashreport/breadcrumbs/ansi_renderer.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace android::crashreport::breadcrumbs {

namespace {

using namespace std::string_view_literals;

// ANSI Color Palette
constexpr std::string_view kReset = "\033[0m"sv;
constexpr std::string_view kBold = "\033[1m"sv;
constexpr std::string_view kRed = "\033[31m"sv;
constexpr std::string_view kGray = "\033[90m"sv;
constexpr std::string_view kWhite = "\033[37m"sv;
constexpr std::string_view kBrightRed = "\033[91m"sv;

// Character Constants for Forensic Spine
struct Symbols {
    static constexpr std::string_view kStart = "▼"sv;
    static constexpr std::string_view kEndOk = "○"sv;
    static constexpr std::string_view kEndCancelled = "◌"sv;
    static constexpr std::string_view kEndDeadline = "⧗"sv;
    static constexpr std::string_view kEndBroken = "✕"sv;
    static constexpr std::string_view kEndDisconnected = "⊝"sv;
    static constexpr std::string_view kEndUnknown = "?"sv;
    static constexpr std::string_view kSend = "▶"sv;
    static constexpr std::string_view kRecv = "◀"sv;
    static constexpr std::string_view kMsgSend = "╞"sv;
    static constexpr std::string_view kMsgRecv = "╡"sv;
    static constexpr std::string_view kRibbon = "│"sv;
    static constexpr std::string_view kRibbonCrash = "║"sv;
    static constexpr std::string_view kIdle = "┊"sv;
    static constexpr std::string_view kBridgeHor = "─"sv;
    static constexpr std::string_view kCornerUpRight = "╰"sv;
    static constexpr std::string_view kCornerUpLeft = "╯"sv;
};

// Rotating color palette for gRPC calls
constexpr std::array<std::string_view, 10> kCallColors = {
    "\033[32m"sv,  // Green
    "\033[33m"sv,  // Yellow
    "\033[34m"sv,  // Blue
    "\033[35m"sv,  // Magenta
    "\033[36m"sv,  // Cyan
    "\033[92m"sv,  // Bright Green
    "\033[93m"sv,  // Bright Yellow
    "\033[94m"sv,  // Bright Blue
    "\033[95m"sv,  // Bright Magenta
    "\033[96m"sv,  // Bright Cyan
};

struct RenderEvent {
    const EnrichedBreadcrumb* breadcrumb;
    uint64_t thread_id;
    bool is_crashing_thread;
};

std::string_view PhaseToLabel(GrpcBreadcrumb::Phase phase) {
    switch (phase) {
    case GrpcBreadcrumb::START:
        return "START"sv;
    case GrpcBreadcrumb::PRE_SEND_INITIAL_METADATA:
        return "SEND_META"sv;
    case GrpcBreadcrumb::PRE_SEND_MESSAGE:
        return "SEND_MSG"sv;
    case GrpcBreadcrumb::PRE_SEND_STATUS:
        return "SEND_STAT"sv;
    case GrpcBreadcrumb::PRE_RECV_INITIAL_METADATA:
        return "RECV_META"sv;
    case GrpcBreadcrumb::PRE_RECV_MESSAGE:
        return "RECV_MSG"sv;
    case GrpcBreadcrumb::PRE_RECV_STATUS:
        return "RECV_STAT"sv;
    case GrpcBreadcrumb::END_OF_CALL:
        return "END"sv;
    default:
        return "UNKNOWN"sv;
    }
}

std::string_view GetCallColor(const DiagnosticTrace& trace, uint32_t call_id, bool use_color) {
    if (!use_color) return ""sv;
    auto it = trace.calls.find(call_id);
    if (it == trace.calls.end()) return kReset;
    return kCallColors[it->second.color_slot % kCallColors.size()];
}

std::string_view GetStatusSymbol(GrpcBreadcrumb::GrpcStatusCode status) {
    switch (status) {
    case GrpcBreadcrumb::OK:
        return Symbols::kEndOk;
    case GrpcBreadcrumb::CANCELLED:
        return Symbols::kEndCancelled;
    case GrpcBreadcrumb::DEADLINE_EXCEEDED:
        return Symbols::kEndDeadline;
    case GrpcBreadcrumb::INTERNAL:
    case GrpcBreadcrumb::DATA_LOSS:
        return Symbols::kEndBroken;
    case GrpcBreadcrumb::UNAVAILABLE:
        return Symbols::kEndDisconnected;
    default:
        return Symbols::kEndUnknown;
    }
}

std::string_view GetMessageSymbol(GrpcBreadcrumb::Phase phase) {
    switch (phase) {
    case GrpcBreadcrumb::PRE_SEND_INITIAL_METADATA:
    case GrpcBreadcrumb::PRE_SEND_MESSAGE:
    case GrpcBreadcrumb::PRE_SEND_STATUS:
        return Symbols::kMsgSend;
    case GrpcBreadcrumb::PRE_RECV_INITIAL_METADATA:
    case GrpcBreadcrumb::PRE_RECV_MESSAGE:
    case GrpcBreadcrumb::PRE_RECV_STATUS:
        return Symbols::kMsgRecv;
    default:
        return "●"sv;
    }
}

/**
 * @brief Internal state for a single render pass to avoid parameter bloat.
 */
struct RenderState {
    const DiagnosticTrace& trace;
    bool use_color;
    std::vector<uint64_t> sorted_tids;
    absl::flat_hash_map<uint64_t, size_t> tid_to_lane;

    // Tracking active calls per lane to draw ribbons.
    absl::flat_hash_map<uint32_t, size_t> call_to_last_lane;
    absl::flat_hash_map<uint32_t, uint64_t> call_last_ts;

    std::string_view Color(std::string_view code) const { return use_color ? code : ""sv; }
};

// 1. Thread Legend
void RenderLegend(const RenderState& state, std::stringstream& ss) {
    ss << state.Color(kBold) << "THREADS\n" << state.Color(kReset);
    for (size_t i = 0; i < state.sorted_tids.size(); ++i) {
        const bool is_crash = (state.sorted_tids[i] == state.trace.crashing_thread_id);
        const std::string label = is_crash ? "[*]" : "[" + std::to_string(i) + "]";
        ss << state.Color(kWhite) << std::setw(4) << label << state.Color(kReset) << " "
           << state.sorted_tids[i];
        if (is_crash) ss << " (Crashing Thread)";
        ss << (i % 4 == 3 || i == state.sorted_tids.size() - 1 ? "\n" : "   ");
    }
    ss << "\n";
}

// 2. Forensic Header
void RenderHeader(const RenderState& state, std::stringstream& ss) {
    ss << state.Color(kBold) << std::left << std::setw(11) << "REL. TIME" << "  ";
    for (size_t i = 0; i < state.sorted_tids.size(); ++i) {
        const std::string label =
                (state.sorted_tids[i] == state.trace.crashing_thread_id) ? "*" : std::to_string(i);
        ss << std::setw(3) << label;
    }
    ss << "  gRPC FORENSIC TIMELINE" << state.Color(kReset) << "\n";
    ss << std::string(11 + 2 + (state.sorted_tids.size() * 3) + 26, '-') << "\n";
}

/**
 * @brief Renders the "Spine" part of the trace row (the thread lanes).
 *
 * Each lane is exactly 3 characters wide.
 * Char 0: The primary symbol (event, ribbon, or idle).
 * Char 1-2: Spacing or horizontal bridge line.
 */
void RenderForensicSpine(RenderState& state, const RenderEvent& re, std::stringstream& ss) {
    const auto& proto = re.breadcrumb->proto;
    const uint32_t cid = proto.call_id();
    const uint64_t current_ts = proto.timestamp_ns();
    const size_t current_lane = state.tid_to_lane[re.thread_id];
    const std::string_view c_color = GetCallColor(state.trace, cid, state.use_color);
    const std::string_view reset = state.Color(kReset);

    bool has_migration = false;
    size_t prev_lane = 0;
    if (state.call_to_last_lane.count(cid) && state.call_to_last_lane[cid] != current_lane) {
        has_migration = true;
        prev_lane = state.call_to_last_lane[cid];
    }

    const size_t min_lane = has_migration ? std::min(prev_lane, current_lane) : current_lane;
    const size_t max_lane = has_migration ? std::max(prev_lane, current_lane) : current_lane;

    for (size_t i = 0; i < state.sorted_tids.size(); ++i) {
        const bool is_crash_lane = (state.sorted_tids[i] == state.trace.crashing_thread_id);

        // Character 0: The Symbol
        if (i == current_lane) {
            ss << c_color;
            if (proto.phase() == GrpcBreadcrumb::END_OF_CALL) {
                ss << GetStatusSymbol(proto.status_code());
            } else if (has_migration) {
                // Use arrowhead for arrival
                ss << (prev_lane < current_lane ? Symbols::kSend : Symbols::kRecv);
            } else if (proto.phase() != GrpcBreadcrumb::PHASE_UNKNOWN &&
                       proto.phase() != GrpcBreadcrumb::START) {
                ss << GetMessageSymbol(proto.phase());
            } else {
                ss << Symbols::kStart;
            }
            ss << reset;
        } else if (has_migration && i == prev_lane) {
            // Source junction: corner pointing towards destination.
            ss << c_color << (i < current_lane ? Symbols::kCornerUpRight : Symbols::kCornerUpLeft)
               << reset;
        } else if (has_migration && i > min_lane && i < max_lane) {
            // Horizontal bridge crossing this lane.
            ss << c_color << Symbols::kBridgeHor << reset;
        } else {
            // Standard vertical ribbon or idle state.
            uint32_t active_cid = 0;
            uint64_t latest_ts = 0;
            for (const auto& [id, lane] : state.call_to_last_lane) {
                if (lane == i) {
                    auto it = state.trace.calls.find(id);
                    if (it != state.trace.calls.end() && current_ts < it->second.end_ns) {
                        if (state.call_last_ts[id] > latest_ts) {
                            latest_ts = state.call_last_ts[id];
                            active_cid = id;
                        }
                    }
                }
            }
            if (active_cid != 0) {
                ss << GetCallColor(state.trace, active_cid, state.use_color)
                   << (is_crash_lane ? Symbols::kRibbonCrash : Symbols::kRibbon) << reset;
            } else {
                ss << state.Color(kGray) << Symbols::kIdle << reset;
            }
        }

        // Characters 1-2: Inter-lane bridge or spacing.
        if (has_migration && i >= min_lane && i < max_lane) {
            ss << c_color << Symbols::kBridgeHor << Symbols::kBridgeHor << reset;
        } else {
            ss << "  ";
        }
    }

    // Update state for next event.
    state.call_to_last_lane[cid] = current_lane;
    state.call_last_ts[cid] = current_ts;
}

}  // namespace

std::string AnsiRenderer::Render(const DiagnosticTrace& trace) const {
    RenderState state{.trace = trace, .use_color = use_color_};
    std::vector<RenderEvent> all_events;

    for (const auto& lane : trace.lanes) {
        state.sorted_tids.push_back(lane.thread_id);
        state.tid_to_lane[lane.thread_id] = state.sorted_tids.size() - 1;
        for (const auto& event : lane.events) {
            all_events.push_back({&event, lane.thread_id, lane.is_crashing_thread});
        }
    }

    std::ranges::sort(all_events, [](const RenderEvent& a, const RenderEvent& b) {
        return a.breadcrumb->proto.timestamp_ns() < b.breadcrumb->proto.timestamp_ns();
    });

    std::stringstream ss;
    RenderLegend(state, ss);
    RenderHeader(state, ss);

    for (const auto& re : all_events) {
        const auto& b = *re.breadcrumb;
        const auto& proto = b.proto;
        const uint64_t rel_ns = proto.timestamp_ns() - trace.global_start_ns;
        const std::string_view c_color = GetCallColor(trace, proto.call_id(), use_color_);
        const std::string_view reset = state.Color(kReset);

        ss << state.Color(kWhite) << std::right << std::setw(11)
           << absl::StrFormat("+%v", absl::Nanoseconds(rel_ns)) << reset << "  ";

        RenderForensicSpine(state, re, ss);

        // Call Narrative (Right side text).
        // Format: [ID] PHASE: Method     { Payload }   [STATUS]
        const std::string call_info = absl::StrFormat("[%d] %s: %s", proto.call_id(),
                                                      PhaseToLabel(proto.phase()), b.method_name);
        ss << "  " << c_color << std::left << std::setw(35) << call_info;

        if (!b.resolved_payload.empty()) {
            ss << " { " << state.Color(kGray) << b.resolved_payload << c_color << " }";
        } else {
            ss << "   ";  // Maintain spacing if no payload
        }

        if (proto.status_code() != GrpcBreadcrumb::OK &&
            proto.phase() == GrpcBreadcrumb::END_OF_CALL) {
            ss << "   " << state.Color(kRed) << state.Color(kBold)
               << "[ERR: " << static_cast<int>(proto.status_code()) << "]" << reset;
        } else {
            ss << reset;
        }
        ss << "\n";
    }

    // Explicitly demarcate the crash site if the last event was an error on the crashing thread.
    if (!all_events.empty()) {
        const auto& last = all_events.back();
        if (last.is_crashing_thread && last.breadcrumb->proto.status_code() != GrpcBreadcrumb::OK) {
            const std::string rule(13 + (state.sorted_tids.size() * 3) + 26, '=');
            ss << state.Color(kBrightRed) << state.Color(kBold) << rule << "\n";
            ss << "FATAL EXCEPTION AT "
               << absl::Nanoseconds(last.breadcrumb->proto.timestamp_ns() - trace.global_start_ns)
               << "\n";
            ss << rule << state.Color(kReset) << "\n";
        }
    }

    return ss.str();
}

}  // namespace android::crashreport::breadcrumbs
