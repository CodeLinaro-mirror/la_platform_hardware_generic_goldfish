#include "android/base/file_system_watcher.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "android/base/file/file.h"

namespace android::base {

using ::testing::Contains;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Property;

using namespace std::chrono_literals;

// A struct to hold the results of a file system watch event. Using a struct

// is cleaner than a tuple for use with Google Mock matchers like `Field`.

struct WatchResult {
    FileSystemWatcher::WatcherChangeType type;

    std::string path;
};

// TestEventHandler is a helper class that manages the state and synchronization

// for the FileSystemWatcher tests. It collects events in a thread-safe manner.

class TestEventHandler {
  public:
    explicit TestEventHandler(fs::path expected_path)
            : mExpectedPath(std::move(expected_path))
            , mNotification(std::make_unique<absl::Notification>()) {}

    // The callback function passed to the FileSystemWatcher.
    void operator()(FileSystemWatcher::WatcherChangeType change, const fs::path& path) {
        // Only store events for the specific file we're testing and resolve symlinks etc.
        // Compare string paths directly.
        bool paths_match = (path == mExpectedPath);
        if (!paths_match) {
            std::error_code ec1, ec2;
            auto p1 = std::filesystem::weakly_canonical(mExpectedPath, ec1);
            auto p2 = std::filesystem::weakly_canonical(path, ec2);
            if (!ec1 && !ec2 && p1 == p2) {
                paths_match = true;
            }
        }

        if (paths_match) {
            absl::MutexLock lock(&mMutex);
            mChanges.push_back({change, path.string()});
            // Prevent aborting by only notifying once.
            if (!mNotification->HasBeenNotified()) {
                mNotification->Notify();
            }
        }
    }

    // Waits for a notification and returns the captured changes.
    std::vector<WatchResult> waitForChange(absl::Duration timeout) {
        if (!mNotification->WaitForNotificationWithTimeout(timeout)) {
            return {};
        }
        absl::MutexLock lock(&mMutex);
        return std::move(mChanges);
    }

    void reset() {
        absl::MutexLock lock(&mMutex);
        mChanges.clear();
        // Re-create the notification to allow waiting again.
        mNotification = std::make_unique<absl::Notification>();
    }

  private:
    fs::path mExpectedPath;
    absl::Mutex mMutex;
    std::unique_ptr<absl::Notification> mNotification;
    std::vector<WatchResult> mChanges;
};

class FileSystemWatcherTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mTempDir = std::filesystem::temp_directory_path() / "fs_watcher_test" /
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        android::base::file::mkdir_recursive(mTempDir, 0755).IgnoreError();
    }

    void TearDown() override {
        if (mWatcher) {
            mWatcher->Stop();
        }
        if (android::base::file::exists(mTempDir)) {
            android::base::file::rm_recursive(mTempDir).IgnoreError();
        }
    }

    void createDir(const std::string& name) {
        android::base::file::mkdir(name, 0755).IgnoreError();
    }

    void deleteDir(const std::string& name) { android::base::file::rm(name).IgnoreError(); }

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

    std::unique_ptr<FileSystemWatcher> mWatcher;
    fs::path mTempDir;
};

TEST_F(FileSystemWatcherTest, DetectsFileCreation) {
    auto expected_path = mTempDir / "test_file1.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    createFile("test_file1.txt");

    auto changes = handler->waitForChange(absl::Seconds(5));
    bool event_found = false;
    for (const auto& change : changes) {
        if (change.type == FileSystemWatcher::WatcherChangeType::kCreated ||
            change.type == FileSystemWatcher::WatcherChangeType::kChanged) {
            event_found = true;
            break;
        }
    }
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, DetectsFileDeletion) {
    auto expected_path = mTempDir / "test_file2.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    // First, create the file and wait for the initial events to clear.
    createFile("test_file2.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());
    handler->waitForChange(absl::Seconds(5));
    handler->reset();

    // Now, delete the file and check for the deletion event.
    deleteFile("test_file2.txt");

    auto changes = handler->waitForChange(absl::Seconds(5));
    bool event_found = false;
    for (const auto& change : changes) {
        if (change.type == FileSystemWatcher::WatcherChangeType::kDeleted ||
            change.type == FileSystemWatcher::WatcherChangeType::kChanged) {
            event_found = true;
            break;
        }
    }
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, DetectsFileLastModifiedTimestampModification) {
    auto expected_path = mTempDir / "test_file3.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    // First, create the file and set up the watcher.
    createFile("test_file3.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    // Now, modify the file and wait for the "Changed" event.
    modifyFile("test_file3.txt");
    auto changes = handler->waitForChange(absl::Seconds(5));
    bool event_found = false;
    for (const auto& change : changes) {
        if (change.type == FileSystemWatcher::WatcherChangeType::kChanged ||
            change.type == FileSystemWatcher::WatcherChangeType::kCreated) {
            event_found = true;
            break;
        }
    }
    EXPECT_TRUE(event_found);
}

TEST_F(FileSystemWatcherTest, DetectsFileSizeModification) {
    auto expected_path = mTempDir / "test_file4.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    // First, create the file and set up the watcher.
    createFile("test_file4.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());
    handler->waitForChange(absl::Seconds(5));
    handler->reset();

    // Now, modify the file and wait for the "Changed" event.
    appendFile("test_file4.txt");
    auto changes = handler->waitForChange(absl::Seconds(10));
    bool event_found = false;
    for (const auto& change : changes) {
        if (change.type == FileSystemWatcher::WatcherChangeType::kChanged ||
            change.type == FileSystemWatcher::WatcherChangeType::kCreated) {
            event_found = true;
            break;
        }
    }
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

    // We expect a timeout because the watcher is stopped.
    auto changes = handler->waitForChange(absl::Seconds(1));
    EXPECT_THAT(changes, IsEmpty());
}
}  // namespace android::base
