// Copyright (C) 2025 The Android Open Source Project
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
#include "goldfish/emulator_catalog.h"

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>

#include "absl/log/log.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace goldfish {

using namespace std::chrono_literals;

class EmulatorCatalogTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create a unique temporary directory for each test.
        mTempDir = std::filesystem::temp_directory_path() / "emulator_catalog_test" /
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(mTempDir);
    }

    void TearDown() override {
        mCatalog.reset();
        // Check if the directory exists before trying to remove it.
        if (std::filesystem::exists(mTempDir)) {
            std::filesystem::remove_all(mTempDir);
        }
    }

    void writeIniFile(const std::string& name, const std::string& content) {
        std::ofstream ofs(mTempDir / name);
        ofs << content;
        ofs.close();
        ASSERT_TRUE(std::filesystem::exists(mTempDir / name))
                << "Failed to write" << (mTempDir / name);
    }

    void deleteIniFile(const std::string& name) {
        auto toRemove = mTempDir / name;
        VLOG(1) << "Removing: " << toRemove;
        std::filesystem::remove(toRemove);
    }

    std::filesystem::path mTempDir;
    std::unique_ptr<EmulatorCatalog> mCatalog;
};

TEST_F(EmulatorCatalogTest, InitialScanFindsExistingFiles) {
    writeIniFile("emu-1.ini", "avd.name=test1\nport.adb=5555");
    writeIniFile("emu-2.ini", "avd.name=test2\nport.adb=5557");

    mCatalog = EmulatorCatalog::create(mTempDir);
    ASSERT_NE(mCatalog, nullptr);

    auto emulators = mCatalog->listEmulators();
    ASSERT_EQ(2, emulators.size());
}

// TODO FIX flakey test
TEST_F(EmulatorCatalogTest, DISABLED_EmulatorAddedEventFires) {
    mCatalog = EmulatorCatalog::create(mTempDir);
    ASSERT_NE(mCatalog, nullptr);

    std::promise<CatalogEntry> entryPromise;
    auto entryFuture = entryPromise.get_future();

    auto handle = makeScopedCallback(mCatalog->emulatorAdded, [&](const CatalogEntry& entry) {
        entryPromise.set_value(entry);
    });

    writeIniFile("emu-added.ini", "avd.name=added\nport.adb=5559");

    // Wait for the event to fire, with a timeout.
    ASSERT_EQ(std::future_status::ready, entryFuture.wait_for(1s));
    auto addedEntry = entryFuture.get();

    EXPECT_EQ("added", addedEntry.properties.at("avd.name"));
    EXPECT_THAT(addedEntry.path.string(), ::testing::HasSubstr("emu-added.ini"));
}

TEST_F(EmulatorCatalogTest, EmulatorRemovedEventFires) {
    using namespace std::chrono_literals;
    writeIniFile("emu-to-remove.ini", "avd.name=toberemoved\nport.adb=5561");
    mCatalog = EmulatorCatalog::create(mTempDir);
    ASSERT_NE(mCatalog, nullptr);

    // Make sure it's there first.
    ASSERT_EQ(1, mCatalog->listEmulators().size());

    std::promise<CatalogEntry> entryPromise;
    auto entryFuture = entryPromise.get_future();
    auto handle = makeScopedCallback(mCatalog->emulatorRemoved, [&](const CatalogEntry& entry) {
        entryPromise.set_value(entry);
    });

    deleteIniFile("emu-to-remove.ini");

    // Wait for the event to fire, with a timeout.
    ASSERT_EQ(std::future_status::ready, entryFuture.wait_for(1s));
    auto removedEntry = entryFuture.get();

    EXPECT_EQ("toberemoved", removedEntry.properties.at("avd.name"));
    EXPECT_TRUE(mCatalog->listEmulators().empty());
}

TEST_F(EmulatorCatalogTest, DISABLED_ListEmulatorsIsCorrectAfterMultipleChanges) {
    mCatalog = EmulatorCatalog::create(mTempDir);
    ASSERT_NE(mCatalog, nullptr);

    std::mutex mtx;
    std::condition_variable cv;
    int eventCount = 0;

    auto addHandle = makeScopedCallback(mCatalog->emulatorAdded, [&](const CatalogEntry&) {
        std::lock_guard<std::mutex> lock(mtx);
        eventCount++;
        cv.notify_one();
    });
    auto removeHandle = makeScopedCallback(mCatalog->emulatorRemoved, [&](const CatalogEntry&) {
        std::lock_guard<std::mutex> lock(mtx);
        eventCount++;
        cv.notify_one();
    });

    auto waitForEvents = [&](int expectedCount) {
        std::unique_lock<std::mutex> lock(mtx);
        return cv.wait_for(lock, 1s, [&] { return eventCount >= expectedCount; });
    };

    writeIniFile("emu-1.ini", "avd.name=test1");
    ASSERT_TRUE(waitForEvents(1));
    writeIniFile("emu-2.ini", "avd.name=test2");
    ASSERT_TRUE(waitForEvents(2));
    ASSERT_EQ(2, mCatalog->listEmulators().size());

    writeIniFile("emu-3.ini", "avd.name=test3");
    ASSERT_TRUE(waitForEvents(3));
    ASSERT_EQ(3, mCatalog->listEmulators().size());

    deleteIniFile("emu-1.ini");
    ASSERT_TRUE(waitForEvents(4));
    auto emulators = mCatalog->listEmulators();
    ASSERT_EQ(2, emulators.size());

    // Check that the correct one was removed.
    for (const auto& entry : emulators) {
        EXPECT_NE("test1", entry.properties.at("avd.name"));
    }
}

TEST_F(EmulatorCatalogTest, IgnoresNonIniFiles) {
    mCatalog = EmulatorCatalog::create(mTempDir);
    ASSERT_NE(mCatalog, nullptr);

    std::promise<void> promise;
    auto future = promise.get_future();

    // This callback should not be called.
    auto handle = makeScopedCallback(mCatalog->emulatorAdded,
                                     [&](const CatalogEntry&) { promise.set_value(); });

    writeIniFile("text.txt", "hello");

    // We expect this to time out.
    ASSERT_EQ(std::future_status::timeout, future.wait_for(150ms));
    EXPECT_TRUE(mCatalog->listEmulators().empty());
}

}  // namespace goldfish
