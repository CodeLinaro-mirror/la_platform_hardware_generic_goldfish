#include "android/files/FileSystemWatcher.h"

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
    explicit TestEventHandler(const std::string& expected_path)
            : mExpectedPath(expected_path), mNotification(std::make_unique<absl::Notification>()) {}

    // The callback function passed to the FileSystemWatcher.
    void operator()(FileSystemWatcher::WatcherChangeType change, const std::string& path) {
        LOG(INFO) << "Change: " << (int)change << " path: " << path << " == " << mExpectedPath;

        // Only store events for the specific file we're testing and resolve symlinks etc.
        if (path == mExpectedPath || std::filesystem::weakly_canonical(mExpectedPath) ==
                                             std::filesystem::weakly_canonical(path)) {
            absl::MutexLock lock(&mMutex);
            mChanges.push_back({change, path});
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
    std::string mExpectedPath;
    absl::Mutex mMutex;
    std::unique_ptr<absl::Notification> mNotification;
    std::vector<WatchResult> mChanges;
};

class FileSystemWatcherTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mTempDir = std::filesystem::temp_directory_path() / "fs_watcher_test" /
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(mTempDir);
    }

    void TearDown() override {
        if (mWatcher) {
            mWatcher->stop();
        }
        if (std::filesystem::exists(mTempDir)) {
            std::filesystem::remove_all(mTempDir);
        }
    }

    void createFile(const std::string& name) {
        std::ofstream ofs(mTempDir / name);
        ofs << "content";
        ofs.close();
    }

    void modifyFile(const std::string& name) {
        auto path = mTempDir / name;
        auto now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(path, now);
    }

    void deleteFile(const std::string& name) { std::filesystem::remove(mTempDir / name); }

    std::unique_ptr<FileSystemWatcher> mWatcher;
    std::filesystem::path mTempDir;
};

TEST_F(FileSystemWatcherTest, DetectsFileDeletion) {
    auto expected_path = (mTempDir / "test_file2.txt").string();
    TestEventHandler handler(expected_path);

    // First, create the file and wait for the initial events to clear.
    createFile("test_file2.txt");
    mWatcher = FileSystemWatcher::getFileSystemWatcher(mTempDir.string(), std::ref(handler));
    ASSERT_TRUE(mWatcher->start());
    handler.waitForChange(absl::Seconds(5));
    handler.reset();

    // Now, delete the file and check for the deletion event.
    deleteFile("test_file2.txt");

    auto changes = handler.waitForChange(absl::Seconds(5));
    EXPECT_THAT(changes,
                Contains(Field(&WatchResult::type, FileSystemWatcher::WatcherChangeType::Deleted)));
}

TEST_F(FileSystemWatcherTest, DetectsFileModification) {
    auto expected_path = (mTempDir / "test_file3.txt").string();
    TestEventHandler handler(expected_path);

    // First, create the file and set up the watcher.
    createFile("test_file3.txt");
    mWatcher = FileSystemWatcher::getFileSystemWatcher(mTempDir.string(), std::ref(handler));
    ASSERT_TRUE(mWatcher->start());

    // Wait for the initial create/modify events and clear them.
    handler.waitForChange(absl::Seconds(5));
    handler.reset();

    // Now, modify the file and wait for the "Changed" event.
    modifyFile("test_file3.txt");
    auto changes = handler.waitForChange(absl::Seconds(5));

    EXPECT_THAT(changes,
                Contains(Field(&WatchResult::type, FileSystemWatcher::WatcherChangeType::Changed)));
}

TEST_F(FileSystemWatcherTest, StopPreventsFurtherEvents) {
    absl::Notification notification;

    mWatcher = FileSystemWatcher::getFileSystemWatcher(
            mTempDir.string(), [&](FileSystemWatcher::WatcherChangeType, const std::string&) {
                notification.Notify();
            });
    ASSERT_TRUE(mWatcher->start());
    mWatcher->stop();

    createFile("test_file4.txt");

    // We expect a timeout because the watcher is stopped.
    EXPECT_FALSE(notification.WaitForNotificationWithTimeout(absl::Seconds(1)));
}

}  // namespace android::base
