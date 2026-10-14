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
#include "goldfish/perfetto/qemu_trace_bridge.h"

#if defined(CONFIG_TRACE_GOLDFISH)

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "absl/strings/match.h"

#include "goldfish/perfetto/perfetto_categories.h"

namespace goldfish::perfetto {
namespace {

struct ThreadTraceState {
    static constexpr size_t kMaxArgs = 16;
    const char* current_event_name = nullptr;
    std::array<GoldfishTraceData, kMaxArgs> args{};
    size_t num_args = 0;
    std::vector<std::string_view> open_slice_prefixes;
};

void AddTraceArgsToContext(::perfetto::EventContext& ctx, const ThreadTraceState& state) {
    for (size_t i = 0; i < state.num_args; ++i) {
        const GoldfishTraceData& arg = state.args[i];
        const auto key = ::perfetto::DynamicString{arg.name ? arg.name : "arg"};
        switch (arg.type) {
        case GOLDFISH_TRACE_ARG_BOOL:
            ctx.AddDebugAnnotation(key, arg.value.b);
            break;
        case GOLDFISH_TRACE_ARG_INT64:
            ctx.AddDebugAnnotation(key, arg.value.i);
            break;
        case GOLDFISH_TRACE_ARG_UINT64:
            ctx.AddDebugAnnotation(key, arg.value.u);
            break;
        case GOLDFISH_TRACE_ARG_POINTER:
            ctx.AddDebugAnnotation(key, arg.value.p);
            break;
        case GOLDFISH_TRACE_ARG_STRING:
            ctx.AddDebugAnnotation(key, arg.value.s ? arg.value.s : "NULL");
            break;
        case GOLDFISH_TRACE_ARG_STRING_ARRAY:
            if (arg.value.sa && arg.value.sa[0]) {
                ctx.AddDebugAnnotation(key, arg.value.sa[0]);
            }
            break;
        case GOLDFISH_TRACE_EVENT_START:
        case GOLDFISH_TRACE_EVENT_END:
            break;
        }
    }
}

::perfetto::DynamicString MakeDynamicName(std::string_view sv) {
    return ::perfetto::DynamicString{sv.data(), sv.size()};
}

}  // namespace

void OnQemuGoldfishTrace(const GoldfishTraceData* data) {
    if (!data || !TRACE_EVENT_CATEGORY_ENABLED("qemu")) {
        return;
    }

    thread_local ThreadTraceState state;

    if (data->type == GOLDFISH_TRACE_EVENT_START) {
        state.current_event_name = data->name;
        state.num_args = 0;
        return;
    }

    if (data->type != GOLDFISH_TRACE_EVENT_END) {
        if (state.current_event_name != nullptr && state.num_args < ThreadTraceState::kMaxArgs) {
            state.args[state.num_args++] = *data;
        }
        return;
    }

    const char* raw_name = state.current_event_name ? state.current_event_name : data->name;
    state.current_event_name = nullptr;
    if (!raw_name) {
        state.num_args = 0;
        return;
    }

    const std::string_view name_sv(raw_name);
    constexpr std::string_view kBeginSuffix = "_begin";
    constexpr std::string_view kEndSuffix = "_end";

    if (absl::EndsWith(name_sv, kBeginSuffix)) {
        const std::string_view prefix = name_sv.substr(0, name_sv.size() - kBeginSuffix.size());
        state.open_slice_prefixes.push_back(prefix);
        TRACE_EVENT_BEGIN("qemu", MakeDynamicName(prefix),
                          [](::perfetto::EventContext ctx) { AddTraceArgsToContext(ctx, state); });
    } else if (absl::EndsWith(name_sv, kEndSuffix)) {
        const std::string_view prefix = name_sv.substr(0, name_sv.size() - kEndSuffix.size());
        if (!state.open_slice_prefixes.empty() && state.open_slice_prefixes.back() == prefix) {
            state.open_slice_prefixes.pop_back();
            TRACE_EVENT_END("qemu", [](::perfetto::EventContext ctx) {
                AddTraceArgsToContext(ctx, state);
            });
        } else {
            TRACE_EVENT_INSTANT(
                    "qemu", ::perfetto::StaticString{raw_name},
                    [](::perfetto::EventContext ctx) { AddTraceArgsToContext(ctx, state); });
        }
    } else {
        TRACE_EVENT_INSTANT(
                "qemu", ::perfetto::StaticString{raw_name},
                [](::perfetto::EventContext ctx) { AddTraceArgsToContext(ctx, state); });
    }

    state.num_args = 0;
}

}  // namespace goldfish::perfetto

#endif  // defined(CONFIG_TRACE_GOLDFISH)
