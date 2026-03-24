/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/metrics/studio_file_metrics_writer.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "android/base/testing/TestTempDir.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "google_logs_publishing.pb.h"

namespace goldfish::metrics {

namespace fs = std::filesystem;
using android::base::TestTempDir;
using goldfish::async::testing::TestEventLoop;

class StudioFileMetricsWriterTest : public ::testing::Test {
  protected:
    void SetUp() override { mTempDir = std::make_unique<TestTempDir>("file_metrics_writer_test"); }

    void TearDown() override { mTempDir.reset(); }

    fs::path temp_path() const { return mTempDir->Path(); }

    std::unique_ptr<TestTempDir> mTempDir;
};

TEST_F(StudioFileMetricsWriterTest, WriteEvent) {
    std::string sessionId = "test-session";
    auto main_loop = TestEventLoop::Create();
    {
        StudioFileMetricsWriter writer(temp_path(), sessionId, *main_loop);
        android_studio::AndroidStudioEvent event;
        event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
        writer.Write({.time_ms = 1234, .as_event = std::move(event)});
    }

    // Check that a .trk file exists
    bool found = false;
    for (const auto& entry : fs::directory_iterator(temp_path())) {
        if (entry.path().extension() == ".trk") {
            found = true;
            std::ifstream is(entry.path(), std::ios::binary);
            google::protobuf::io::IstreamInputStream raw_input(&is);
            google::protobuf::io::CodedInputStream coded_input(&raw_input);

            uint32_t size;
            ASSERT_TRUE(coded_input.ReadVarint32(&size));

            wireless_android_play_playlog::LogEvent log_event;
            auto limit = coded_input.PushLimit(size);
            ASSERT_TRUE(log_event.MergeFromCodedStream(&coded_input));
            coded_input.PopLimit(limit);

            EXPECT_EQ(log_event.event_time_ms(), 1234);

            android_studio::AndroidStudioEvent read_event;
            ASSERT_TRUE(read_event.ParseFromString(log_event.source_extension()));
            EXPECT_EQ(read_event.kind(), android_studio::AndroidStudioEvent::EMULATOR_PING);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(StudioFileMetricsWriterTest, WriteMultipleEvents) {
    std::string sessionId = "test-session-multiple";
    auto main_loop = TestEventLoop::Create();
    {
        StudioFileMetricsWriter writer(temp_path(), sessionId, *main_loop);
        for (int i = 0; i < 5; ++i) {
            android_studio::AndroidStudioEvent event;
            event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
            writer.Write(
                    {.time_ms = static_cast<uint64_t>(1000 + i), .as_event = std::move(event)});
        }
    }

    int file_count = 0;
    for (const auto& entry : fs::directory_iterator(temp_path())) {
        if (entry.path().extension() == ".trk") {
            file_count++;
            std::ifstream is(entry.path(), std::ios::binary);
            google::protobuf::io::IstreamInputStream raw_input(&is);
            google::protobuf::io::CodedInputStream coded_input(&raw_input);

            for (int i = 0; i < 5; ++i) {
                uint32_t size;
                ASSERT_TRUE(coded_input.ReadVarint32(&size));

                wireless_android_play_playlog::LogEvent log_event;
                auto limit = coded_input.PushLimit(size);
                ASSERT_TRUE(log_event.MergeFromCodedStream(&coded_input));
                coded_input.PopLimit(limit);

                EXPECT_EQ(log_event.event_time_ms(), 1000 + i);
            }
        }
    }
    EXPECT_EQ(file_count, 1);
}

TEST_F(StudioFileMetricsWriterTest, FileRotationByRecordCount) {
    std::string sessionId = "test-session-rotation-count";
    auto main_loop = TestEventLoop::Create();
    {
        StudioFileMetricsWriter writer(temp_path(), sessionId, *main_loop);
        // kMaxRecordsPerFile is 1000.
        for (int i = 0; i < 1001; ++i) {
            android_studio::AndroidStudioEvent event;
            event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
            writer.Write({.time_ms = static_cast<uint64_t>(i), .as_event = std::move(event)});
        }
    }

    int trk_file_count = 0;
    for (const auto& entry : fs::directory_iterator(temp_path())) {
        if (entry.path().extension() == ".trk") {
            trk_file_count++;
        }
    }
    // 1000 records in the first file (finalized after 1000th Write),
    // 1 record in the second file (finalized by destructor).
    EXPECT_EQ(trk_file_count, 2);
}

// TODO(whollins): Enable this test once we have a mockable clock interface
// (e.g. absl::time/clock_interface.h) available in our abseil version.
TEST_F(StudioFileMetricsWriterTest, DISABLED_FileRotationByTime) {
    std::string sessionId = "test-session-rotation-time";
    auto main_loop = TestEventLoop::Create();
    {
        StudioFileMetricsWriter writer(temp_path(), sessionId, *main_loop);

        android_studio::AndroidStudioEvent event;
        event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
        writer.Write({.time_ms = 100, .as_event = std::move(event)});

        // Initial file is .open
        int open_count = 0;
        for (const auto& entry : fs::directory_iterator(temp_path())) {
            if (entry.path().extension() == ".open") open_count++;
        }
        EXPECT_EQ(open_count, 1);

        // kMaxFileDuration is 10 minutes.
        // The timer runs every 10s.
        main_loop->AdvanceClock(std::chrono::minutes(11));
        main_loop->RunAll();

        // Now it should be .trk
        int trk_count = 0;
        for (const auto& entry : fs::directory_iterator(temp_path())) {
            if (entry.path().extension() == ".trk") trk_count++;
        }
        EXPECT_EQ(trk_count, 1);
    }
}

TEST_F(StudioFileMetricsWriterTest, DestructorFinalizesFile) {
    std::string sessionId = "test-session-destructor";
    auto main_loop = TestEventLoop::Create();
    {
        StudioFileMetricsWriter writer(temp_path(), sessionId, *main_loop);
        android_studio::AndroidStudioEvent event;
        event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
        writer.Write({.time_ms = 100, .as_event = std::move(event)});

        // File should be .open
        bool found_open = false;
        for (const auto& entry : fs::directory_iterator(temp_path())) {
            if (entry.path().extension() == ".open") found_open = true;
        }
        EXPECT_TRUE(found_open);
    }
    // Writer destroyed, file should be .trk
    bool found_trk = false;
    for (const auto& entry : fs::directory_iterator(temp_path())) {
        if (entry.path().extension() == ".trk") found_trk = true;
    }
    EXPECT_TRUE(found_trk);
}

}  // namespace goldfish::metrics
