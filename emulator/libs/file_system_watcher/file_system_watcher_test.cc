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

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/cleanup/cleanup.h"
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

    bool operator==(const WatchResult& other) const {
        return type == other.type && path == other.path;
    }

    friend std::ostream& operator<<(std::ostream& os, const WatchResult& result) {
        os << "{type=";
        switch (result.type) {
        case FileSystemWatcher::WatcherChangeType::kCreated:
            os << "Created";
            break;
        case FileSystemWatcher::WatcherChangeType::kChanged:
            os << "Changed";
            break;
        case FileSystemWatcher::WatcherChangeType::kDeleted:
            os << "Deleted";
            break;
        default:
            os << static_cast<int>(result.type);
            break;
        }
        return os << ", path=" << result.path << "}";
    }
};

class TestEventHandler {
  public:
    TestEventHandler(fs::path expected_path) : mExpectedPath(std::move(expected_path)) {}

    void operator()(FileSystemWatcher::WatcherChangeType type, const std::filesystem::path& path) {
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
#ifdef __APPLE__
        // see:
        // https://stackoverflow.com/questions/57825757/obj-c-has-available-swift-has-available-what-can-i-use-in-c-c
        // see:
        // https://raw.githubusercontent.com/facebook/watchman/main/website/docs/troubleshooting.md
        if (!__builtin_available(macOS 14.0, *)) {
            GTEST_SKIP() << "Skipping live FSEvents test on macOS < 14 (Ventura fseventsd bug).";
        }
#endif
        // Create a watchdog to detect hangs.
        mTestDone = std::make_unique<absl::Notification>();
        mWatchdog = std::thread([this]() {
            if (!mTestDone->WaitForNotificationWithTimeout(absl::Seconds(180))) {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                fprintf(stderr, "FATAL: Test timed out after 180 seconds. Possible hang in %s.%s\n",
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
        if (!mTempDir.empty() && android::base::file::exists(mTempDir)) {
            android::base::file::rm_recursive(mTempDir).IgnoreError();
        }
    }

    void createDir(const std::string& name) {
        android::base::file::mkdir(mTempDir / name, 0755).IgnoreError();
    }

    void deleteDir(const std::string& name) {
        android::base::file::rm(mTempDir / name).IgnoreError();
    }

    bool createFile(const std::string& name) {
        std::ofstream ofs(mTempDir / name);
        ofs << "content";
        ofs.close();
        return ofs.good();
    }

    bool appendFile(const std::string& name) {
        std::ofstream ofs;
        ofs.open(mTempDir / name, std::ios_base::app);
        ofs << "\nextra content";
        ofs.close();
        return ofs.good();
    }

    bool modifyFile(const std::string& name) {
        auto path = mTempDir / name;
        std::error_code ec;
        auto now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(path, now, ec);
        return !ec;
    }

    bool deleteFile(const std::string& name) {
        auto result = android::base::file::rm(mTempDir / name);
        return result.ok();
    }

    bool waitForCanary(std::shared_ptr<TestEventHandler> handler) {
        // 1. Create the canary file.
        if (!createFile("__canary__")) return false;

        auto deadline = absl::Now() + absl::Seconds(5);
        while (absl::Now() < deadline) {
            // 2. Wait a short time (e.g., 100ms) to see if the event was captured.
            bool event_found = WaitForEvent(
                    handler,
                    [](const WatchResult& change) {
                        return change.path.find("__canary__") != std::string::npos;
                    },
                    absl::Milliseconds(100));

            if (event_found) {
                handler->reset();
                return true;  // The watcher is live!
            }

            // 3. If not found, modify the file to trigger a new event.
            // This forces FSEvents to generate a new event if it is now active.
            modifyFile("__canary__");
        }

        return false;  // Timed out after 5 seconds
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
    ASSERT_TRUE(waitForCanary(handler));

    ASSERT_TRUE(createFile("test_file1.txt"));

    bool event_found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("test_file1.txt") != std::string::npos &&
                       (change.type == FileSystemWatcher::WatcherChangeType::kCreated ||
                        change.type == FileSystemWatcher::WatcherChangeType::kChanged);
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

    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());
    ASSERT_TRUE(waitForCanary(handler));

    ASSERT_TRUE(createFile("test_file2.txt"));
    WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("test_file2.txt") != std::string::npos;
            },
            absl::Seconds(5));
    handler->reset();

    ASSERT_TRUE(deleteFile("test_file2.txt"));

    bool event_found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("test_file2.txt") != std::string::npos &&
                       (change.type == FileSystemWatcher::WatcherChangeType::kDeleted ||
                        change.type == FileSystemWatcher::WatcherChangeType::kChanged);
            },
            absl::Seconds(5));
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, DetectsFileLastModifiedTimestampModification) {
    auto expected_path = mTempDir / "test_file3.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());
    ASSERT_TRUE(waitForCanary(handler));

    ASSERT_TRUE(createFile("test_file3.txt"));
    WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("test_file3.txt") != std::string::npos;
            },
            absl::Seconds(5));
    handler->reset();

    ASSERT_TRUE(modifyFile("test_file3.txt"));

    bool event_found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("test_file3.txt") != std::string::npos &&
                       (change.type == FileSystemWatcher::WatcherChangeType::kChanged ||
                        change.type == FileSystemWatcher::WatcherChangeType::kCreated);
            },
            absl::Seconds(5));
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, DetectsFileSizeModification) {
    auto expected_path = mTempDir / "test_file4.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());
    ASSERT_TRUE(waitForCanary(handler));

    ASSERT_TRUE(createFile("test_file4.txt"));
    WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("test_file4.txt") != std::string::npos;
            },
            absl::Seconds(5));
    handler->reset();

    ASSERT_TRUE(appendFile("test_file4.txt"));

    bool event_found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("test_file4.txt") != std::string::npos &&
                       (change.type == FileSystemWatcher::WatcherChangeType::kChanged ||
                        change.type == FileSystemWatcher::WatcherChangeType::kCreated);
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

    // Clear any spurious events from setting up the test environment.
    handler->reset();

    ASSERT_TRUE(createFile("test_file4.txt"));

    auto changes = handler->waitForChange(absl::Seconds(1));
    EXPECT_THAT(changes, IsEmpty());
}

TEST_F(FileSystemWatcherTest, DetectsFileRename) {
    auto handler = std::make_shared<TestEventHandler>(mTempDir / "renamed_dst.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());
    ASSERT_TRUE(waitForCanary(handler));

    ASSERT_TRUE(createFile("renamed_src.txt"));
    ASSERT_TRUE(WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("renamed_src.txt") != std::string::npos;
            },
            absl::Seconds(5)));
    handler->reset();

    std::error_code ec;
    std::filesystem::rename(mTempDir / "renamed_src.txt", mTempDir / "renamed_dst.txt", ec);
    ASSERT_FALSE(ec);

    bool dst_created = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("renamed_dst.txt") != std::string::npos &&
                       (change.type == FileSystemWatcher::WatcherChangeType::kCreated ||
                        change.type == FileSystemWatcher::WatcherChangeType::kChanged);
            },
            absl::Seconds(3));
    EXPECT_TRUE(dst_created);
}

TEST_F(FileSystemWatcherTest, RestartSameInstanceAndConcurrentStop) {
    auto handler = std::make_shared<TestEventHandler>(mTempDir / "restart_file.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_NE(mWatcher, nullptr);

    // First lifecycle
    ASSERT_TRUE(mWatcher->Start());
    std::thread stopper1([this]() { mWatcher->Stop(); });
    std::thread stopper2([this]() { mWatcher->Stop(); });
    stopper1.join();
    stopper2.join();

    // Restart the same instance and verify it still delivers events
    ASSERT_TRUE(mWatcher->Start());
    ASSERT_TRUE(waitForCanary(handler));
    handler->reset();

    ASSERT_TRUE(createFile("restart_file.txt"));
    bool found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("restart_file.txt") != std::string::npos;
            },
            absl::Seconds(5));
    EXPECT_TRUE(found);
    mWatcher->Stop();
}

#ifndef _WIN32
TEST_F(FileSystemWatcherTest, StartsAndStopsWhenStdinClosed) {
    const int saved_stdin = dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);
    const absl::Cleanup restore_stdin = [this, saved_stdin] {
        if (mWatcher) {
            mWatcher->Stop();
        }
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
    };
    close(STDIN_FILENO);

    auto handler = std::make_shared<TestEventHandler>(mTempDir / "stdin_closed.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_NE(mWatcher, nullptr);
    ASSERT_TRUE(mWatcher->Start());
    ASSERT_TRUE(waitForCanary(handler));
    handler->reset();

    ASSERT_TRUE(createFile("stdin_closed.txt"));
    bool found = WaitForEvent(
            handler,
            [](const WatchResult& change) {
                return change.path.find("stdin_closed.txt") != std::string::npos;
            },
            absl::Seconds(5));
    EXPECT_TRUE(found);
    mWatcher->Stop();
#ifdef __linux__
    EXPECT_EQ(fcntl(STDIN_FILENO, F_GETFD), -1);
#endif
}
#endif

}  // namespace android::base
