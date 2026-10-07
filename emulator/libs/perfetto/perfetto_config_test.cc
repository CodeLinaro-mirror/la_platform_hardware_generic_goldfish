/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "android/base/testing/test_temp_dir.h"
#include "goldfish/perfetto/perfetto.h"
#include "goldfish/perfetto/perfetto_categories.h"
#include "goldfish/perfetto/qemu_trace_bridge.h"

namespace goldfish::perfetto {
namespace {

/**
 * @brief Checks if a trace file contains a specific event name.
 *
 * Rationale:
 * Ideally, we would use Perfetto's generated C++ classes to parse the trace protobuf
 * and verify the events structurally. However, those generated headers are not
 * exposed to this module in the current build setup.
 *
 * As a fallback, this function performs a simple string search in the binary trace data.
 * Even when Perfetto uses string interning, the string literal will still appear in the
 * trace file at least once (within the InternedData packet), making this a reliable
 * check for the presence of the event.
 */
bool TraceContainsEvent(const std::filesystem::path& trace_file, const std::string& event_name) {
    std::ifstream input(trace_file, std::ios::binary);
    if (!input.is_open()) return false;

    std::vector<char> buffer((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());

    std::string data(buffer.begin(), buffer.end());
    return data.find(event_name) != std::string::npos;
}

class PerfettoConfigTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() { Initialize(); }
};

TEST_F(PerfettoConfigTest, CanTraceInstantEvent) {
    android::base::TestTempDir tmp_dir("perfetto_test");
    ASSERT_FALSE(tmp_dir.Path().empty());
    std::filesystem::path trace_file = tmp_dir.MakeSubPath("instant_events.pftrace");

    EmulatorTracingSession session(trace_file, "rendering,async");
    TRACE_EVENT_INSTANT("rendering", "TestInstantEvent");
    session.Stop();
    EXPECT_TRUE(std::filesystem::exists(trace_file));
}

TEST_F(PerfettoConfigTest, CanTraceRenderingEvent) {
    android::base::TestTempDir tmp_dir("perfetto_test");
    ASSERT_FALSE(tmp_dir.Path().empty());
    std::filesystem::path trace_file = tmp_dir.MakeSubPath("rendering_events.pftrace");

    {
        EmulatorTracingSession session(trace_file, "rendering,async");

        TRACE_EVENT("rendering", "FrameRender");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        TRACE_EVENT("rendering", "DrawPrimitives", "count", 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(std::filesystem::exists(trace_file));
    EXPECT_TRUE(TraceContainsEvent(trace_file, "FrameRender"));
    EXPECT_TRUE(TraceContainsEvent(trace_file, "DrawPrimitives"));
}

TEST_F(PerfettoConfigTest, CanTraceAsyncEvent) {
    android::base::TestTempDir tmp_dir("perfetto_test");
    ASSERT_FALSE(tmp_dir.Path().empty());
    std::filesystem::path trace_file = tmp_dir.MakeSubPath("async_events.pftrace");

    {
        EmulatorTracingSession session(trace_file, "rendering,async");

        TRACE_EVENT_BEGIN("async", "AsyncOperation");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        TRACE_EVENT_END("async");
    }

    EXPECT_TRUE(std::filesystem::exists(trace_file));
    EXPECT_TRUE(TraceContainsEvent(trace_file, "AsyncOperation"));
}

TEST_F(PerfettoConfigTest, BridgesQemuTraceEventsAndArgumentsToPerfetto) {
    android::base::TestTempDir tmp_dir("perfetto_qemu_bridge_test");
    ASSERT_FALSE(tmp_dir.Path().empty());
    std::filesystem::path trace_file = tmp_dir.MakeSubPath("qemu_bridge.pftrace");

    {
        EmulatorTracingSession session(trace_file, "qemu");

        // 1. Scoped _begin / _end pair (e.g. aio_bh_call_begin / aio_bh_call_end)
        GoldfishTraceData start_begin{.type = GOLDFISH_TRACE_EVENT_START,
                                      .name = "aio_bh_call_begin"};
        GoldfishTraceData bh_name_arg{.type = GOLDFISH_TRACE_ARG_STRING, .name = "bh_name"};
        bh_name_arg.value.s = "qemu_event_loop_bh_cb";
        GoldfishTraceData end_begin{.type = GOLDFISH_TRACE_EVENT_END, .name = "aio_bh_call_begin"};
        OnQemuGoldfishTrace(&start_begin);
        OnQemuGoldfishTrace(&bh_name_arg);
        OnQemuGoldfishTrace(&end_begin);

        // 2. Nested instant event with arguments (e.g. virtqueue_fill)
        GoldfishTraceData vq_start{.type = GOLDFISH_TRACE_EVENT_START, .name = "virtqueue_fill"};
        GoldfishTraceData vq_len{.type = GOLDFISH_TRACE_ARG_UINT64, .name = "elem_len"};
        vq_len.value.u = 4096;
        GoldfishTraceData vq_end{.type = GOLDFISH_TRACE_EVENT_END, .name = "virtqueue_fill"};
        OnQemuGoldfishTrace(&vq_start);
        OnQemuGoldfishTrace(&vq_len);
        OnQemuGoldfishTrace(&vq_end);

        // Close the scoped aio_bh_call slice
        GoldfishTraceData start_end{.type = GOLDFISH_TRACE_EVENT_START, .name = "aio_bh_call_end"};
        GoldfishTraceData end_end{.type = GOLDFISH_TRACE_EVENT_END, .name = "aio_bh_call_end"};
        OnQemuGoldfishTrace(&start_end);
        OnQemuGoldfishTrace(&end_end);
    }

    EXPECT_TRUE(std::filesystem::exists(trace_file));
    EXPECT_TRUE(TraceContainsEvent(trace_file, "aio_bh_call"));
    EXPECT_TRUE(TraceContainsEvent(trace_file, "qemu_event_loop_bh_cb"));
    EXPECT_TRUE(TraceContainsEvent(trace_file, "virtqueue_fill"));
    EXPECT_TRUE(TraceContainsEvent(trace_file, "elem_len"));
}

TEST_F(PerfettoConfigTest, TraceNonceGeneratesUniqueMonotonicIds) {
    const uint64_t first = TRACE_NONCE();
    const uint64_t second = TRACE_NONCE();
    EXPECT_GT(first, 0U);
    EXPECT_EQ(second, first + 1);
}

}  // namespace
}  // namespace goldfish::perfetto
