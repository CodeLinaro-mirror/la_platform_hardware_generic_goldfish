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

#include "android/base/testing/TestTempDir.h"
#include "goldfish/perfetto/perfetto.h"
#include "goldfish/perfetto/perfetto_categories.h"
#include "perfetto/tracing/tracing.h"
#include "perfetto/tracing/track_event.h"

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

}  // namespace
}  // namespace goldfish::perfetto
