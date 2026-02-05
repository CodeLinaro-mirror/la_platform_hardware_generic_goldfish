#include "android/base/file_system_watcher.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

#include "absl/log/log.h"
#include "absl/random/random.h"
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
    explicit TestEventHandler(fs::path expected_path) : mExpectedPath(std::move(expected_path)) {}

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
            absl::MutexLock lock(mMutex);
            mChanges.push_back({change, path.string()});
        }
    }

    // Waits for a notification and returns the captured changes.
    std::vector<WatchResult> waitForChange(absl::Duration timeout) {
        absl::MutexLock lock(mMutex);
        auto has_changes = [this]() { return !mChanges.empty(); };
        mMutex.AwaitWithTimeout(absl::Condition(&has_changes), timeout);

        std::vector<WatchResult> result;
        std::swap(result, mChanges);
        return result;
    }

    void reset() {
        absl::MutexLock lock(mMutex);
        mChanges.clear();
    }

  private:
    fs::path mExpectedPath;
    absl::Mutex mMutex;
    std::vector<WatchResult> mChanges;
};

class FileSystemWatcherTest : public ::testing::Test {
  protected:
    static std::string RandomSafeString(size_t size) {
        absl::BitGen bitgen;
        constexpr std::string_view kCharset =
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
        // Create a unique temporary directory for each test, mainly so we can run tests in
        // parallel. i.e. bazel test @goldfish//android/files:file_system_watcher_test
        // --runs_per_test=50
        do {
            mTempDir = std::filesystem::temp_directory_path() / "fs_watcher_test" /
                       RandomSafeString(15);
        } while (std::filesystem::exists(mTempDir));
        android::base::file::mkdir_recursive(mTempDir, 0755).IgnoreError();
        VLOG(1) << "Created temporary directory: " << mTempDir;
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

    /**
     * @brief Waits for a specific file system event to occur within a timeout.
     *
     * This helper continuously polls the TestEventHandler for new events and checks
     * each one against the provided predicate. This is necessary because file system
     * events can be delivered in multiple batches or contain noise.
     *
     * @tparam Predicate A callable type with signature bool(const WatchResult&).
     * @param handler The event handler collecting file system events.
     * @param pred The condition to match the desired event.
     * @param timeout The maximum duration to wait for the event.
     * @return true if an event matching the predicate was found, false otherwise.
     */
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

TEST_F(FileSystemWatcherTest, DetectsFileDeletion) {
    auto expected_path = mTempDir / "test_file2.txt";
    auto handler = std::make_shared<TestEventHandler>(expected_path);

    // First, create the file and wait for the initial events to clear.
    createFile("test_file2.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    // Wait for creation to settle
    WaitForEvent(handler, [](const auto&) { return true; }, absl::Seconds(5));
    handler->reset();

    // Now, delete the file and check for the deletion event.
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

    // First, create the file and set up the watcher.
    createFile("test_file3.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    // Now, modify the file and wait for the "Changed" event.
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

    // First, create the file and set up the watcher.
    createFile("test_file4.txt");
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            mTempDir, [handler](auto type, auto path) { (*handler)(type, path); });
    ASSERT_TRUE(mWatcher->Start());

    WaitForEvent(handler, [](const auto&) { return true; }, absl::Seconds(5));
    handler->reset();

    // Now, modify the file and wait for the "Changed" event.
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

    // We expect a timeout because the watcher is stopped.
    auto changes = handler->waitForChange(absl::Seconds(1));
    EXPECT_THAT(changes, IsEmpty());
}
}  // namespace android::base
