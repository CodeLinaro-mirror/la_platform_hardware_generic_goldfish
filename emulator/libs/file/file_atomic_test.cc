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
#include "goldfish/file/file_atomic.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "android/base/system.h"
#include "goldfish/file/file.h"

namespace android::base::file {

class FileAtomicTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Use a temporary directory for each test
        temp_dir_ = fs::temp_directory_path() / "FileAtomicTest";
        fs::create_directories(temp_dir_);
    }

    void TearDown() override { fs::remove_all(temp_dir_); }

    fs::path temp_dir_;
};

TEST_F(FileAtomicTest, WriteNewFile) {
    fs::path target = temp_dir_ / "test_file.txt";
    std::string content = "Hello, World!";

    auto status = CreatePrivateFileExclusive(target, content);
    EXPECT_TRUE(status.ok()) << status.message();
    EXPECT_TRUE(fs::exists(target));

    std::ifstream ifs(target);
    std::string actual_content((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
    EXPECT_EQ(actual_content, content);
}

TEST_F(FileAtomicTest, FailsIfFileExists) {
    fs::path target = temp_dir_ / "exclusive.txt";
    ASSERT_TRUE(CreatePrivateFileExclusive(target, "First").ok());

    // Subsequent write should fail
    auto status = CreatePrivateFileExclusive(target, "Second");
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kAlreadyExists);

    std::ifstream ifs(target);
    std::string actual_content((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
    EXPECT_EQ(actual_content, "First");  // Content should not have changed
}

TEST_F(FileAtomicTest, PrivatePermissions) {
    fs::path target = temp_dir_ / "private.txt";
    ASSERT_TRUE(CreatePrivateFileExclusive(target, "private").ok());

    auto perms = fs::status(target).permissions();

#ifdef _WIN32
    // Windows permissions are handled via ACLs
#else
    // On POSIX, we expect 0600 (owner read/write)
    EXPECT_EQ(perms, fs::perms::owner_read | fs::perms::owner_write);
#endif
}

}  // namespace android::base::file
