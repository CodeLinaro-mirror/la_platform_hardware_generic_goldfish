/* Copyright 2025 The Android Open Source Project
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

#include "android/base/file_system_watcher.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "goldfish/file/file.h"

namespace android::base {

using namespace ::testing;
namespace fs = std::filesystem;

struct WatchResult {
    FileSystemWatcher::WatcherChangeType type;
    std::string path;
};

class TestEventHandler {
  public:
    TestEventHandler(fs::path expected_path) : mExpectedPath(std::move(expected_path)) {}

    void operator()(FileSystemWatcher::WatcherChangeType type,
                    const FileSystemWatcher::Path& path) {
        absl::MutexLock lock(mMutex);
        mResults.push_back({type, path.string()});
    }

    std::vector<WatchResult> waitForChange(absl::Duration timeout) {
        absl::MutexLock lock(mMutex);
        auto condition = [this]() { return !mResults.empty(); };
        if (mMutex.AwaitWithTimeout(absl::Condition(&condition), timeout)) {
            return std::exchange(mResults, {});
        }
        return {};
    }

    void reset() {
        absl::MutexLock lock(mMutex);
        mResults.clear();
    }

  private:
    fs::path mExpectedPath;
    absl::Mutex mMutex;
    std::vector<WatchResult> mResults;
};

class FileSystemWatcherTest : public ::testing::Test {
  public:
    static std::string RandomSafeString(size_t size) {
        absl::BitGen bitgen;
        static constexpr absl::string_view kCharset =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";

        std::string random_string;
        random_string.reserve(size);

        for (int i = 0; i < size; ++i) {
            random_string += kCharset[absl::Uniform(bitgen, 0u, kCharset.size())];
        }
        return random_string;
    }

    void SetUp() override {
        // Create a watchdog to detect hangs.
        mTestDone = std::make_unique<absl::Notification>();
        mWatchdog = std::thread([this]() {
            if (!mTestDone->WaitForNotificationWithTimeout(absl::Seconds(10))) {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                fprintf(stderr, "FATAL: Test timed out after 10 seconds. Possible hang in %s.%s\n",
                        info->test_suite_name(), info->name());
                std::abort();
            }
        });

        // Create a unique temporary directory for each test
        do {
            mTempDir = std::filesystem::temp_directory_path() / "fs_watcher_test" /
                       RandomSafeString(15);
        } while (std::filesystem::exists(mTempDir));
        android::base::file::mkdir_recursive(mTempDir, 0755).IgnoreError();
        VLOG(1) << "Created temporary directory: " << mTempDir;
    }

    void TearDown() override {
        if (mTestDone) {
            mTestDone->Notify();
        }
        if (mWatchdog.joinable()) {
            mWatchdog.join();
        }

        if (mWatcher) {
            mWatcher->Stop();
        }
        if (android::base::file::exists(mTempDir)) {
            android::base::file::rm_recursive(mTempDir).IgnoreError();
        }
    }

    void createDir(const std::string& name) {
        android::base::file::mkdir(mTempDir / name, 0755).IgnoreError();
    }

    void deleteDir(const std::string& name) {
        android::base::file::rm(mTempDir / name).IgnoreError();
    }

    void createFile(const std::string& name) {
        std::ofstream ofs(mTempDir / name);
        ofs << "content";
        ofs.close();
    }

    void appendFile(const std::string& name) {
        std::ofstream ofs;
        ofs.open(mTempDir / name, std::ios_base::app);
        ofs << "\nextra content";
        ofs.close();
    }

    void modifyFile(const std::string& name) {
        auto path = mTempDir / name;
        auto now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(path, now);
    }

    void deleteFile(const std::string& name) {
        android::base::file::rm(mTempDir / name).IgnoreError();
    }

    template <typename Predicate>
    bool WaitForEvent(std::shared_ptr<TestEventHandler> handler, Predicate pred,
                      absl::Duration timeout) {
        auto deadline = absl::Now() + timeout;
        while (absl::Now() < deadline) {
            auto remaining = deadline - absl::Now();
            if (remaining < absl::ZeroDuration()) remaining = absl::ZeroDuration();
            auto changes = handler->waitForChange(remaining);
            for (const auto& change : changes) {
                if (pred(change)) return true;
            }
        }
        return false;
    }

    std::unique_ptr<FileSystemWatcher> mWatcher;
    fs::path mTempDir;
    std::unique_ptr<absl::Notification> mTestDone;
    std::thread mWatchdog;
};

TEST_F(FileSystemWatcherTest, DetectsFileCreation) {
    auto expected_path = mTempDir / "test_file1.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    createFile("test_file1.txt");

    bool event_found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.type == FileSystemWatcher::WatcherChangeType::kCreated ||
                       change.type == FileSystemWatcher::WatcherChangeType::kChanged;
            },
            absl::Seconds(5));
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, StartStopStressTest) {
    for (int i = 0; i < 100; ++i) {
        mWatcher = FileSystemWatcher::GetFileSystemWatcher(mTempDir, [](auto, auto) {});
        ASSERT_NE(mWatcher, nullptr);
        ASSERT_TRUE(mWatcher->Start());
        mWatcher->Stop();
    }
}

TEST_F(FileSystemWatcherTest, DetectsFileDeletion) {
    auto expected_path = mTempDir / "test_file2.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    createFile("test_file2.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    WaitForEvent(handler, [](const auto&) { return true; }, absl::Seconds(5));
    handler->reset();

    deleteFile("test_file2.txt");

    bool event_found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.type == FileSystemWatcher::WatcherChangeType::kDeleted ||
                       change.type == FileSystemWatcher::WatcherChangeType::kChanged;
            },
            absl::Seconds(5));
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, DetectsFileLastModifiedTimestampModification) {
    auto expected_path = mTempDir / "test_file3.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    createFile("test_file3.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    modifyFile("test_file3.txt");

    bool event_found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.type == FileSystemWatcher::WatcherChangeType::kChanged ||
                       change.type == FileSystemWatcher::WatcherChangeType::kCreated;
            },
            absl::Seconds(5));
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, DetectsFileSizeModification) {
    auto expected_path = mTempDir / "test_file4.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    createFile("test_file4.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    WaitForEvent(handler, [](const auto&) { return true; }, absl::Seconds(5));
    handler->reset();

    appendFile("test_file4.txt");

    bool event_found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.type == FileSystemWatcher::WatcherChangeType::kChanged ||
                       change.type == FileSystemWatcher::WatcherChangeType::kCreated;
            },
            absl::Seconds(10));
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, StopPreventsFurtherEvents) {
    auto expected_path = mTempDir / "test_file4.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());
    mWatcher->Stop();

    createFile("test_file4.txt");

    auto changes = handler->waitForChange(absl::Seconds(1));
    EXPECT_THAT(changes, IsEmpty());
}

}  // namespace android::base
