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

#include "android/crashreport/breadcrumbs/util.h"

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

std::string_view PhaseToLabel(android::control::breadcrumbs::GrpcPayload::GrpcPhase phase) {
    using android::control::breadcrumbs::GrpcPayload;
    switch (phase) {
    case GrpcPayload::START:
        return "START"sv;
    case GrpcPayload::PRE_SEND_INITIAL_METADATA:
        return "SEND_META"sv;
    case GrpcPayload::PRE_SEND_MESSAGE:
        return "SEND_MSG"sv;
    case GrpcPayload::PRE_SEND_STATUS:
        return "SEND_STAT"sv;
    case GrpcPayload::PRE_RECV_INITIAL_METADATA:
        return "RECV_META"sv;
    case GrpcPayload::PRE_RECV_MESSAGE:
    case GrpcPayload::POST_RECV_MESSAGE:
        return "RECV_MSG"sv;
    case GrpcPayload::PRE_RECV_STATUS:
        return "RECV_STAT"sv;
    case GrpcPayload::END_OF_CALL:
        return "END"sv;
    default:
        return "UNKNOWN"sv;
    }
}

std::string_view GetCallColor(const DiagnosticTrace& trace, uint64_t call_id, bool use_color) {
    if (!use_color) return ""sv;
    auto it = trace.calls.find(call_id);
    if (it == trace.calls.end()) return kReset;
    return kCallColors[it->second.color_slot % kCallColors.size()];
}

std::string_view GetStatusSymbol(
        android::control::breadcrumbs::GrpcPayload::GrpcStatusCode status) {
    using android::control::breadcrumbs::GrpcPayload;
    switch (status) {
    case GrpcPayload::OK:
        return Symbols::kEndOk;
    case GrpcPayload::CANCELLED:
        return Symbols::kEndCancelled;
    case GrpcPayload::DEADLINE_EXCEEDED:
        return Symbols::kEndDeadline;
    case GrpcPayload::INTERNAL:
    case GrpcPayload::DATA_LOSS:
        return Symbols::kEndBroken;
    case GrpcPayload::UNAVAILABLE:
        return Symbols::kEndDisconnected;
    default:
        return Symbols::kEndUnknown;
    }
}

std::string_view GetMessageSymbol(android::control::breadcrumbs::GrpcPayload::GrpcPhase phase) {
    using android::control::breadcrumbs::GrpcPayload;
    switch (phase) {
    case GrpcPayload::PRE_SEND_INITIAL_METADATA:
    case GrpcPayload::PRE_SEND_MESSAGE:
    case GrpcPayload::PRE_SEND_STATUS:
        return Symbols::kMsgSend;
    case GrpcPayload::PRE_RECV_INITIAL_METADATA:
    case GrpcPayload::PRE_RECV_MESSAGE:
    case GrpcPayload::PRE_RECV_STATUS:
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
    size_t time_width = 11;
    size_t tid_width = 3;

    std::string_view Color(std::string_view code) const { return use_color ? code : ""sv; }
};

// 2. Forensic Header
void RenderHeader(const RenderState& state, std::stringstream& ss) {
    ss << state.Color(kGray) << "(* : Crashing Thread, numbers are Breakpad thread IDs)\n"
       << state.Color(kReset);
    ss << state.Color(kBold) << std::left << std::setw(state.time_width) << "REL. TIME" << "  ";
    for (size_t i = 0; i < state.sorted_tids.size(); ++i) {
        const std::string label = (state.sorted_tids[i] == state.trace.crashing_thread_id)
                                          ? "*"
                                          : std::to_string(state.sorted_tids[i]);
        ss << std::setw(state.tid_width) << label;
    }
    ss << "  FORENSIC TIMELINE" << state.Color(kReset) << "\n";
    ss << std::string(state.time_width + 2 + (state.sorted_tids.size() * state.tid_width) + 26, '-')
       << "\n";
}

// 2b. Compact Forensic Header for repetition
void RenderCompactHeader(const RenderState& state, std::stringstream& ss) {
    ss << state.Color(kGray);
    ss << std::string(state.time_width, ' ') << "  ";
    for (size_t i = 0; i < state.sorted_tids.size(); ++i) {
        const std::string label = (state.sorted_tids[i] == state.trace.crashing_thread_id)
                                          ? "*"
                                          : std::to_string(state.sorted_tids[i]);
        ss << std::setw(state.tid_width) << label;
    }
    ss << state.Color(kReset) << "\n";
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
    const uint64_t cid = proto.flow_id();
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
            if (proto.has_grpc()) {
                const auto& grpc = proto.grpc();
                if (grpc.grpc_phase() == android::control::breadcrumbs::GrpcPayload::END_OF_CALL) {
                    ss << GetStatusSymbol(grpc.status_code());
                } else if (has_migration) {
                    ss << (prev_lane < current_lane ? Symbols::kSend : Symbols::kRecv);
                } else if (grpc.grpc_phase() !=
                                   android::control::breadcrumbs::GrpcPayload::PHASE_UNKNOWN &&
                           grpc.grpc_phase() != android::control::breadcrumbs::GrpcPayload::START) {
                    ss << GetMessageSymbol(grpc.grpc_phase());
                } else {
                    ss << Symbols::kStart;
                }
            } else if (proto.has_adb()) {
                if (proto.phase() == Breadcrumb::FLOW_END) {
                    ss << Symbols::kEndOk;
                } else if (has_migration) {
                    ss << (prev_lane < current_lane ? Symbols::kSend : Symbols::kRecv);
                } else if (proto.phase() == Breadcrumb::FLOW_STEP) {
                    ss << "●"sv;
                } else {
                    ss << Symbols::kStart;
                }
            } else if (proto.has_looper()) {
                const auto& looper = proto.looper();
                if (looper.event() == android::control::breadcrumbs::LooperPayload::EXECUTE) {
                    if (has_migration) {
                        ss << (prev_lane < current_lane ? Symbols::kSend : Symbols::kRecv);
                    } else {
                        ss << Symbols::kEndOk;
                    }
                } else {
                    if (has_migration) {
                        ss << (prev_lane < current_lane ? Symbols::kSend : Symbols::kRecv);
                    } else {
                        ss << Symbols::kStart;
                    }
                }
            } else {
                ss << " "sv;
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
            ss << c_color;
            for (size_t j = 0; j < state.tid_width - 1; ++j) {
                ss << Symbols::kBridgeHor;
            }
            ss << reset;
        } else {
            ss << std::string(state.tid_width - 1, ' ');
        }
    }

    // Update state for next event.
    state.call_to_last_lane[cid] = current_lane;
    state.call_last_ts[cid] = current_ts;
}

struct TimeFormattingResult {
    size_t max_width;
    std::vector<std::string> formatted_times;
};

TimeFormattingResult CalculateTimeWidths(const std::vector<RenderEvent>& events,
                                         uint64_t global_start_ns) {
    size_t max_time_width = 45;
    std::vector<std::string> formatted_times;
    formatted_times.reserve(events.size());
    for (const auto& re : events) {
        const uint64_t ts_ns = re.breadcrumb->proto.timestamp_ns();
        std::string t = FormatEventTime(ts_ns, global_start_ns);
        max_time_width = std::max(max_time_width, t.length());
        formatted_times.push_back(std::move(t));
    }
    return {max_time_width, std::move(formatted_times)};
}

}  // namespace

std::string AnsiRenderer::Render(const DiagnosticTrace& trace) const {
    RenderState state{.trace = trace, .use_color = use_color_};
    std::vector<RenderEvent> all_events;

    size_t max_tid_width = 3;
    for (const auto& lane : trace.lanes) {
        state.sorted_tids.push_back(lane.thread_id);
        state.tid_to_lane[lane.thread_id] = state.sorted_tids.size() - 1;
        max_tid_width = std::max(max_tid_width, std::to_string(lane.thread_id).length());
        for (const auto& event : lane.events) {
            all_events.push_back({&event, lane.thread_id, lane.is_crashing_thread});
        }
    }
    state.tid_width = max_tid_width;

    std::ranges::sort(all_events, [](const RenderEvent& a, const RenderEvent& b) {
        return a.breadcrumb->proto.timestamp_ns() < b.breadcrumb->proto.timestamp_ns();
    });

    auto [max_time_width, formatted_times] = CalculateTimeWidths(all_events, trace.global_start_ns);
    state.time_width = max_time_width;

    std::stringstream ss;
    RenderHeader(state, ss);

    for (size_t i = 0; i < all_events.size(); ++i) {
        if (i > 0 && i % 40 == 0) {
            RenderCompactHeader(state, ss);
        }
        const auto& re = all_events[i];
        const auto& b = *re.breadcrumb;
        const auto& proto = b.proto;
        const std::string_view c_color = GetCallColor(trace, proto.flow_id(), use_color_);
        const std::string_view reset = state.Color(kReset);

        ss << state.Color(kWhite) << std::right << std::setw(state.time_width) << formatted_times[i]
           << reset << "  ";

        RenderForensicSpine(state, re, ss);

        // Call Narrative (Right side text).
        // Format: [ID] PHASE: Method     { Payload }   [STATUS]
        std::string_view label = "UNKNOWN";
        uint64_t call_id = proto.flow_id();
        if (proto.has_grpc()) {
            label = PhaseToLabel(proto.grpc().grpc_phase());
        } else if (proto.has_adb()) {
            label = "ADB";
        } else if (proto.has_looper()) {
            switch (proto.looper().event()) {
            case android::control::breadcrumbs::LooperPayload::POST:
                label = "POST";
                break;
            case android::control::breadcrumbs::LooperPayload::EXECUTE:
                label = "EXEC";
                break;
            default:
                label = "LOOPER";
                break;
            }
        }

        const std::string call_info =
                absl::StrFormat("[%v] %s: %s (T%v)", call_id, label, b.method_name, re.thread_id);
        ss << "  " << c_color << std::left << std::setw(35) << call_info;

        if (!b.resolved_payload.empty()) {
            ss << " { " << state.Color(kGray) << b.resolved_payload << c_color << " }";
        } else {
            ss << "   ";  // Maintain spacing if no payload
        }

        if (proto.has_grpc() &&
            proto.grpc().status_code() != android::control::breadcrumbs::GrpcPayload::OK &&
            proto.grpc().grpc_phase() == android::control::breadcrumbs::GrpcPayload::END_OF_CALL) {
            ss << "   " << state.Color(kRed) << state.Color(kBold)
               << "[ERR: " << static_cast<int>(proto.grpc().status_code()) << "]" << reset;
        } else {
            ss << reset;
        }
        ss << "\n";
    }

    // Explicitly demarcate the crash site if the last event was an error on the
    // crashing thread.
    if (!all_events.empty()) {
        const auto& last = all_events.back();
        if (last.is_crashing_thread && last.breadcrumb->proto.has_grpc() &&
            last.breadcrumb->proto.grpc().status_code() !=
                    android::control::breadcrumbs::GrpcPayload::OK) {
            const std::string rule(state.time_width + 2 + (state.sorted_tids.size() * 3) + 26, '=');
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
