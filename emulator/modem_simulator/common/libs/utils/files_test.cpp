/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "utils/files.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace cuttlefish {
namespace fs = std::filesystem;

class FileUtilsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create a unique temporary directory for each test fixture
        // to avoid conflicts between tests.
        temp_dir_ = fs::temp_directory_path() /
                    ("cuttlefish_test_" +
                     std::to_string(
                             std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        // Clean up the temporary directory after each test
        fs::remove_all(temp_dir_);
    }

    fs::path CreateTempFile(const std::string& filename, const std::string& content = "") {
        auto full_path = (temp_dir_ / fs::path(filename));
        std::ofstream ofs(full_path);
        ofs << content;
        ofs.close();
        return full_path;
    }

    fs::path temp_dir_;
};

TEST_F(FileUtilsTest, FileExists_ExistingFile) {
    auto path = CreateTempFile("existing_file.txt");
    EXPECT_TRUE(FileExists(path));
}

TEST_F(FileUtilsTest, FileExists_NonExistingFile) {
    auto non_existing_path = (temp_dir_ / "non_existing_file.txt");
    EXPECT_FALSE(FileExists(non_existing_path));
}

TEST_F(FileUtilsTest, FileExists_Directory) {
    EXPECT_TRUE(FileExists(temp_dir_));
}

TEST_F(FileUtilsTest, FileHasContent_NonExistingFile) {
    auto non_existing_path = (temp_dir_ / "empty.txt");
    EXPECT_FALSE(FileHasContent(non_existing_path));
}

TEST_F(FileUtilsTest, FileHasContent_EmptyFile) {
    auto path = CreateTempFile("empty_file.txt", "");
    EXPECT_FALSE(FileHasContent(path));
}

TEST_F(FileUtilsTest, FileHasContent_FileWithContent) {
    auto path = CreateTempFile("content_file.txt", "Some content");
    EXPECT_TRUE(FileHasContent(path));
}

TEST_F(FileUtilsTest, FileSize_NonExistingFile) {
    auto non_existing_path = (temp_dir_ / "non_existent.txt");
    EXPECT_EQ(FileSize(non_existing_path), -1);
}

TEST_F(FileUtilsTest, FileSize_EmptyFile) {
    std::string path = CreateTempFile("empty_file.txt", "");
    EXPECT_EQ(FileSize(path), 0);
}

TEST_F(FileUtilsTest, FileSize_SmallFile) {
    std::string content = "Hello, World!";
    std::string path = CreateTempFile("small_file.txt", content);
    EXPECT_EQ(FileSize(path), content.length());
}

TEST_F(FileUtilsTest, ReadFile_NonExistingFile) {
    std::string non_existing_path = (temp_dir_ / "no_read.txt").string();
    EXPECT_TRUE(ReadFile(non_existing_path).empty());
}

TEST_F(FileUtilsTest, ReadFile_EmptyFile) {
    std::string path = CreateTempFile("empty_read.txt", "");
    EXPECT_TRUE(ReadFile(path).empty());
}

TEST_F(FileUtilsTest, ReadFile_FileWithContent) {
    std::string content = "This is some test content.";
    std::string path = CreateTempFile("read_me.txt", content);
    EXPECT_EQ(ReadFile(path), content);
}

TEST_F(FileUtilsTest, ReadFile_FileWithMultiLineContent) {
    std::string content = "Line 1\nLine 2\nLine 3";
    std::string path = CreateTempFile("multi_line.txt", content);
    EXPECT_EQ(ReadFile(path), content);
}

TEST_F(FileUtilsTest, AbsolutePath_RelativePath) {
    std::string relative_path = "test_file.txt";
    std::string full_path = CreateTempFile(relative_path);

    // fs::absolute canonicalizes the path, so we expect it to match
    EXPECT_EQ(AbsolutePath(relative_path), fs::absolute(relative_path).string());
}

TEST_F(FileUtilsTest, AbsolutePath_ExistingAbsolutePath) {
    auto path = CreateTempFile("another_file.txt");
    // If the input is already an absolute path, AbsolutePath should return it as is,
    // or its canonical form.
    EXPECT_EQ(AbsolutePath(path), fs::absolute(path).string());
}

TEST_F(FileUtilsTest, AbsolutePath_NonExistentPath) {
    // AbsolutePath should work even for non-existent paths, as it's purely path manipulation.
    std::string non_existent_relative = "non_existent_dir/non_existent_file.txt";
    std::string expected_absolute = fs::absolute(non_existent_relative).string();
    EXPECT_EQ(AbsolutePath(non_existent_relative), expected_absolute);
}

TEST_F(FileUtilsTest, AllFunctions_UnicodePath) {
    // This test validates that all file utility functions work with Unicode paths.
    // On many modern systems, std::string can hold UTF-8, and
    // std::filesystem/std::fstream can handle it.
    std::string unicode_filename = "œ®´†你好世界.txt";
    std::string content = "Unicode 内容很重要。";

    fs::path unicode_path_fs = CreateTempFile(unicode_filename, content);
    std::string unicode_path_str = unicode_path_fs.string();

    // 1. Test FileExists
    EXPECT_TRUE(FileExists(unicode_path_str));
    fs::path non_existing_unicode_path = temp_dir_ / fs::path("不存在.txt");
    EXPECT_FALSE(FileExists(non_existing_unicode_path.string()));

    EXPECT_TRUE(FileHasContent(unicode_path_str));

    // 3. Test FileSize
    EXPECT_EQ(FileSize(unicode_path_str), content.length());

    // 4. Test ReadFile
    EXPECT_EQ(ReadFile(unicode_path_str), content);

    // 5. Test AbsolutePath on an absolute path
    EXPECT_EQ(AbsolutePath(unicode_path_str), fs::absolute(unicode_path_fs).string());

    // 6. Test AbsolutePath on a relative path
    auto original_path = fs::current_path();
    fs::current_path(temp_dir_);
    EXPECT_EQ(AbsolutePath(unicode_filename), fs::absolute(fs::path(unicode_filename)).string());
}

}  // namespace cuttlefish