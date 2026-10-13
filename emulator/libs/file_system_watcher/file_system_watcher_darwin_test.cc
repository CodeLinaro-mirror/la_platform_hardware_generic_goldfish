// Copyright (C) 2026 The Android Open Source Project
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

#include "android/base/file_system_watcher_darwin.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/base/file_system_watcher.h"
#include "goldfish/file/file.h"

namespace android::base {
namespace {

struct RecordedEvent {
    FileSystemWatcher::WatcherChangeType type;
    std::string path;
};

class FileSystemWatcherDarwinTest : public ::testing::Test {
  protected:
    static std::string RandomSafeString(size_t size) {
        absl::BitGen bitgen;
        static constexpr absl::string_view kCharset =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";

        std::string random_string;
        random_string.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            random_string += kCharset[absl::Uniform(bitgen, 0u, kCharset.size())];
        }
        return random_string;
    }

    void SetUp() override {
        do {
            temp_dir_ = std::filesystem::temp_directory_path() / "fs_watcher_darwin_test" /
                        RandomSafeString(15);
        } while (std::filesystem::exists(temp_dir_));
        android::base::file::mkdir_recursive(temp_dir_, 0755).IgnoreError();
    }

    void TearDown() override {
        if (watcher_) {
            watcher_->Stop();
        }
        if (android::base::file::exists(temp_dir_)) {
            android::base::file::rm_recursive(temp_dir_).IgnoreError();
        }
    }

    bool WriteFile(const std::string& name, const std::string& content) {
        std::ofstream ofs(temp_dir_ / name, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return false;
        }
        ofs << content;
        ofs.flush();
        return ofs.good();
    }

    std::filesystem::path temp_dir_;
    std::unique_ptr<FileSystemWatcher> watcher_;
};

TEST_F(FileSystemWatcherDarwinTest, CoalescedCreateAndDeleteOrdersDeleteOnlyWhenGone) {
    std::vector<RecordedEvent> recorded;
    absl::Mutex recorded_mu;
    FSEventStreamCallback saved_cb = nullptr;
    FSEventStreamRef saved_stream = nullptr;
    void* saved_info = nullptr;

    DarwinWatcherOptions options;
    options.stream_create = [&](CFAllocatorRef alloc, FSEventStreamCallback cb,
                                FSEventStreamContext* ctx, CFArrayRef paths,
                                FSEventStreamEventId since_when, CFTimeInterval latency,
                                FSEventStreamCreateFlags flags) -> FSEventStreamRef {
        saved_stream = FSEventStreamCreate(alloc, cb, ctx, paths, since_when, latency, flags);
        saved_cb = cb;
        saved_info = ctx ? ctx->info : nullptr;
        return saved_stream;
    };

    watcher_ = CreateDarwinFileSystemWatcherForTesting(
            temp_dir_,
            [&](auto type, const auto& path) {
                if (path.string().find("coalesced_gone.txt") != std::string::npos) {
                    const absl::MutexLock lock(recorded_mu);
                    recorded.push_back({type, path.string()});
                }
            },
            std::move(options));
    ASSERT_TRUE(watcher_->Start());
    ASSERT_NE(saved_cb, nullptr);

    std::string deleted_file = (temp_dir_ / "coalesced_gone.txt").string();
    char* path_ptr = deleted_file.data();
    char* event_paths[1] = {path_ptr};
    FSEventStreamEventFlags event_flags[1] = {
        kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemModified |
        kFSEventStreamEventFlagItemRemoved | kFSEventStreamEventFlagItemIsFile};
    FSEventStreamEventId event_ids[1] = {1};
    saved_cb(saved_stream, saved_info, 1, event_paths, event_flags, event_ids);

    watcher_->Stop();

    const absl::MutexLock lock(recorded_mu);
    ASSERT_FALSE(recorded.empty());
    // Because coalesced_gone.txt does not exist on disk, the terminal event MUST be kDeleted,
    // and kCreated must not be emitted after kDeleted.
    EXPECT_EQ(recorded.back().type, FileSystemWatcher::WatcherChangeType::kDeleted);
}

TEST_F(FileSystemWatcherDarwinTest, StreamStartFailureReturnsFalse) {
    DarwinWatcherOptions options;
    options.stream_start = [](FSEventStreamRef) -> Boolean { return false; };

    watcher_ = CreateDarwinFileSystemWatcherForTesting(
            temp_dir_, [](auto, const auto&) {}, std::move(options));
    ASSERT_NE(watcher_, nullptr);
    EXPECT_FALSE(watcher_->Start());
}

TEST_F(FileSystemWatcherDarwinTest, RunLoopSourceCreateFailureReturnsFalseAndDoesNotDeadlock) {
    DarwinWatcherOptions options;
    options.run_loop_source_create = [](CFAllocatorRef, CFIndex,
                                        CFRunLoopSourceContext*) -> CFRunLoopSourceRef {
        return nullptr;
    };

    watcher_ = CreateDarwinFileSystemWatcherForTesting(
            temp_dir_, [](auto, const auto&) {}, std::move(options));
    ASSERT_NE(watcher_, nullptr);
    EXPECT_FALSE(watcher_->Start());
    watcher_->Stop();
    watcher_.reset();
}

TEST_F(FileSystemWatcherDarwinTest, StartupTimeoutPreventsGhostCallbacksAndCleansUp) {
    std::atomic<int> callback_count{0};
    DarwinWatcherOptions options;
    options.startup_timeout = absl::Milliseconds(20);
    options.stream_create = [](CFAllocatorRef alloc, FSEventStreamCallback cb,
                               FSEventStreamContext* ctx, CFArrayRef paths,
                               FSEventStreamEventId since_when, CFTimeInterval latency,
                               FSEventStreamCreateFlags flags) -> FSEventStreamRef {
        absl::SleepFor(absl::Milliseconds(80));
        return FSEventStreamCreate(alloc, cb, ctx, paths, since_when, latency, flags);
    };

    watcher_ = CreateDarwinFileSystemWatcherForTesting(
            temp_dir_, [&](auto, const auto&) { callback_count.fetch_add(1); }, std::move(options));
    ASSERT_NE(watcher_, nullptr);
    // Start() times out after 20ms and returns false.
    EXPECT_FALSE(watcher_->Start());

    // Wait for the delayed stream_create (80ms) to finish and trigger file changes.
    // Because Start() returned false, the watcher must NOT silently enter kRunning
    // and fire ghost callbacks.
    absl::SleepFor(absl::Milliseconds(120));
    ASSERT_TRUE(WriteFile("ghost_after_failed_start.txt", "initial"));
    ASSERT_TRUE(WriteFile("ghost_after_failed_start.txt", "modified"));
    absl::SleepFor(absl::Milliseconds(200));
    EXPECT_EQ(callback_count.load(), 0);
}

TEST_F(FileSystemWatcherDarwinTest, StreamCreateFailureDoesNotTerminateOnDestroyOrRestart) {
    DarwinWatcherOptions options;
    options.stream_create = [](CFAllocatorRef, FSEventStreamCallback, FSEventStreamContext*,
                               CFArrayRef, FSEventStreamEventId, CFTimeInterval,
                               FSEventStreamCreateFlags) -> FSEventStreamRef { return nullptr; };

    watcher_ = CreateDarwinFileSystemWatcherForTesting(
            temp_dir_, [](auto, const auto&) {}, std::move(options));
    ASSERT_NE(watcher_, nullptr);
    EXPECT_FALSE(watcher_->Start());
    // Calling Start() again or destroying watcher_ must not hit std::terminate()
    // due to an unjoined watcher_thread_.
    EXPECT_FALSE(watcher_->Start());
    watcher_.reset();
}

TEST_F(FileSystemWatcherDarwinTest, StartupTimeoutFollowedByStreamCreateFailureDoesNotTerminate) {
    DarwinWatcherOptions options;
    options.startup_timeout = absl::Milliseconds(20);
    options.stream_create = [](CFAllocatorRef, FSEventStreamCallback, FSEventStreamContext*,
                               CFArrayRef, FSEventStreamEventId, CFTimeInterval,
                               FSEventStreamCreateFlags) -> FSEventStreamRef {
        absl::SleepFor(absl::Milliseconds(60));
        return nullptr;
    };

    watcher_ = CreateDarwinFileSystemWatcherForTesting(
            temp_dir_, [](auto, const auto&) {}, std::move(options));
    ASSERT_NE(watcher_, nullptr);
    EXPECT_FALSE(watcher_->Start());
    // Wait for WatchForChanges() to exit after stream_create returns nullptr.
    absl::SleepFor(absl::Milliseconds(100));
    // Destroying watcher_ must join watcher_thread_ cleanly and not call std::terminate().
    watcher_.reset();
}

}  // namespace
}  // namespace android::base
