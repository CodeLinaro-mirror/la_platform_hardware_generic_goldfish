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

#include "android/base/system.h"
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

TEST_F(StudioFileMetricsWriterTest, FinalizeAbandonedFiles) {
    // 1. Create a "dead" abandoned file (no lock file)
    fs::path dead_file = temp_path() / "emulator-metrics2-dead-session-1234-0.open";
    {
        std::ofstream fs(dead_file);
    }

    // 2. Create a "live" abandoned file (with a live lock file)
    // We must use a PID that is actually running to test the "live" status.
    int current_pid = android::base::System::GetCurrentProcessPid();
    fs::path live_file =
            temp_path() / absl::StrFormat("emulator-metrics2-live-session-%d-0.open", current_pid);
    {
        std::ofstream fs(live_file);
    }
    fs::path live_lock =
            temp_path() /
            absl::StrFormat("emulator-metrics2-live-session-%d-0.open.lock", current_pid);
    {
        std::ofstream fs(live_lock);
    }

    auto abandoned = StudioFileMetricsWriter::FinalizeAbandonedSessionFiles(temp_path());

    ASSERT_EQ(abandoned.size(), 1);
    EXPECT_EQ(abandoned[0], "dead-session");

    // dead-session should be .trk now
    EXPECT_FALSE(fs::exists(dead_file));
    EXPECT_TRUE(fs::exists(temp_path() / "emulator-metrics2-dead-session-1234-0.trk"));

    // live-session should still be .open
    EXPECT_TRUE(fs::exists(live_file));
    EXPECT_FALSE(fs::exists(
            temp_path() / absl::StrFormat("emulator-metrics2-live-session-%d-0.trk", current_pid)));
}

TEST_F(StudioFileMetricsWriterTest, FinalizeAbandonedFilesWithDeadPid) {
    // 1. Create a file with a PID that is likely not running (e.g., a very large PID)
    // and a lock file that is "live" (recent).
    // The new implementation should see that the PID is dead and finalize anyway.
    uint32_t dead_pid = 999999;
    fs::path dead_pid_file =
            temp_path() / absl::StrFormat("emulator-metrics2-dead-pid-session-%d-0.open", dead_pid);
    {
        std::ofstream fs(dead_pid_file);
    }
    fs::path dead_pid_lock =
            temp_path() /
            absl::StrFormat("emulator-metrics2-dead-pid-session-%d-0.open.lock", dead_pid);
    {
        std::ofstream fs(dead_pid_lock);
    }

    auto abandoned = StudioFileMetricsWriter::FinalizeAbandonedSessionFiles(temp_path());

    ASSERT_EQ(abandoned.size(), 1);
    EXPECT_EQ(abandoned[0], "dead-pid-session");

    EXPECT_FALSE(fs::exists(dead_pid_file));
    EXPECT_TRUE(
            fs::exists(temp_path() /
                       absl::StrFormat("emulator-metrics2-dead-pid-session-%d-0.trk", dead_pid)));
    EXPECT_FALSE(fs::exists(dead_pid_lock));
}

TEST_F(StudioFileMetricsWriterTest, FinalizeAbandonedFilesWithExpiredLock) {
    // Create a file with an expired lock file.
    fs::path expired_file = temp_path() / "emulator-metrics2-expired-session-1234-0.open";
    {
        std::ofstream fs(expired_file);
    }
    fs::path expired_lock = temp_path() / "emulator-metrics2-expired-session-1234-0.open.lock";
    {
        std::ofstream fs(expired_lock);
    }

    // Manually set lock file time to the past (at least 61 seconds ago).
    auto old_time = std::filesystem::file_time_type::clock::now() - std::chrono::seconds(70);
    std::filesystem::last_write_time(expired_lock, old_time);

    auto abandoned = StudioFileMetricsWriter::FinalizeAbandonedSessionFiles(temp_path());

    ASSERT_EQ(abandoned.size(), 1);
    EXPECT_EQ(abandoned[0], "expired-session");
    EXPECT_TRUE(fs::exists(temp_path() / "emulator-metrics2-expired-session-1234-0.trk"));
    EXPECT_FALSE(fs::exists(expired_lock));
}

TEST_F(StudioFileMetricsWriterTest, FilenameFormatAndCounter) {
    std::string sessionId = "format-test";
    auto main_loop = TestEventLoop::Create();
    StudioFileMetricsWriter writer(temp_path(), sessionId, *main_loop);

    auto write_event = [&](int i) {
        android_studio::AndroidStudioEvent event;
        event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
        writer.Write({.time_ms = static_cast<uint64_t>(i), .as_event = std::move(event)});
    };

    write_event(0);
    uint32_t pid = android::base::System::GetCurrentProcessPid();

    auto check_file = [&](int counter, std::string_view ext) {
        std::string expected =
                absl::StrFormat("emulator-metrics2-%s-%d-%d%s", sessionId, pid, counter, ext);
        EXPECT_TRUE(fs::exists(temp_path() / expected)) << "Missing file: " << expected;
    };

    check_file(0, ".open");
    check_file(0, ".open.lock");

    // Force rotation by record count (1000).
    for (int i = 1; i < 1000; ++i) {
        write_event(i);
    }
    // Now file 0 should be .trk and file 1 should be .open
    check_file(0, ".trk");
    EXPECT_FALSE(fs::exists(temp_path() / (absl::StrFormat("emulator-metrics2-%s-%d-0.open.lock",
                                                           sessionId, pid))));

    write_event(1000);
    check_file(1, ".open");
    check_file(1, ".open.lock");
}

TEST_F(StudioFileMetricsWriterTest, LockFileManagement) {
    std::string sessionId = "lock-test";
    auto main_loop = TestEventLoop::Create();
    int pid = android::base::System::GetCurrentProcessPid();
    fs::path lock_path =
            temp_path() / absl::StrFormat("emulator-metrics2-%s-%d-0.open.lock", sessionId, pid);

    {
        StudioFileMetricsWriter writer(temp_path(), sessionId, *main_loop);
        android_studio::AndroidStudioEvent event;
        event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
        writer.Write({.time_ms = 100, .as_event = std::move(event)});

        // 1. Check creation
        EXPECT_TRUE(fs::exists(lock_path));
        auto initial_time = android::base::file::last_write_time(lock_path).value();

        // 2. Check refresh
        // The timer runs every 10s. Advance enough time for at least one trigger.
        main_loop->AdvanceClock(std::chrono::seconds(11));
        main_loop->RunAll();

        auto refreshed_time = android::base::file::last_write_time(lock_path).value();
        EXPECT_GT(refreshed_time, initial_time);
    }

    // 3. Check deletion on finalization (destructor)
    EXPECT_FALSE(fs::exists(lock_path));
}

}  // namespace goldfish::metrics
