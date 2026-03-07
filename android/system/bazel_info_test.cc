
// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/base/bazel_info.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <vector>

#include "goldfish/file/file.h"

namespace android {
namespace base {

TEST(bazel_info, InBazel) {
    EXPECT_TRUE(Bazel::InBazel());
}

TEST(bazel_info, can_get_data_file) {
    EXPECT_FALSE(Bazel::RunfilesPath("goldfish+/android/system/test/android/base/bazel/info.txt")
                         .empty());
}

TEST(bazel_info, can_read_data_file) {
    std::string path =
            Bazel::RunfilesPath("goldfish+/android/system/test/android/base/bazel/info.txt");
    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "Hello World!");
}

TEST(bazel_info, can_get_non_existent_file) {
    EXPECT_FALSE(base::file::exists(Bazel::RunfilesPath("non/existent/file.txt")));
}

}  // namespace base
}  // namespace android
