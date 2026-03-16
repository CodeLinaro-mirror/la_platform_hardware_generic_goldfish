// Copyright (C) 2015 The Android Open Source Project
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

#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

#include "absl/log/log.h"

#include "goldfish/file/file.h"

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#include <errno.h>
#include <stdio.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include <iostream>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace android::base {

// A class used to model a temporary directory used during testing.
// Usage is simple:
//
//      {
//        TestTempDir myDir("my_test");   // creates new temp directory.
//        ASSERT_TRUE(myDir.Path());      // NULL if error during creation.
//        ... write files into directory Path myDir->Path()
//        ... do your test
//      }   // destructor removes temp directory and all files under it.

class TestTempDir {
  public:
    // Create new instance. This also tries to create a new temporary
    // directory. |debugPrefix| is an optional name prefix and can be empty.
    explicit TestTempDir(std::string_view debug_name) {
        // TODO use System::getTempDir() instead of getTempPath()
        path = GetTempPath() / GenerateRandomString();
        if (!debug_name.empty()) {
            path /= debug_name;
        }

        // path is always absolute
        if (auto res = base::file::make_absolute(path); res.ok()) {
            path = *res;
        } else {
            LOG(FATAL) << "Failed to make absolute Path for " << path
                       << " due to: " << res.status();
        }

        if (base::file::exists(path)) {
            if (auto s = base::file::rm_recursive(path); !s.ok()) {
                LOG(FATAL) << "Failed to remove old test directory: " << path << " due to: " << s;
            }
        }

        // Attempt to create the temporary directory
        if (auto s = base::file::mkdir_recursive(path, 0755); !s.ok()) {
            LOG(FATAL) << "Failed to create " << path << " due to: " << s;
        }
    }

    // Return the Path to the temporary directory, or NULL if it could not
    // be created for some reason.
    fs::path Path() const { return path; }

    // Return the Path as a string. It will be empty if the directory could
    // not be created for some reason.
    std::string PathString() const { return path.string(); }

    // Destroy instance, and removes the temporary directory and all files
    // inside it.
    ~TestTempDir() {
        if (!path.empty()) {
            base::file::rm_recursive(path).IgnoreError();
        }
    }

    TestTempDir(const TestTempDir&) = delete;
    TestTempDir(TestTempDir&&) = delete;
    TestTempDir& operator=(const TestTempDir&) = delete;
    TestTempDir& operator=(TestTempDir&&) = delete;

    // Create the Path of a directory entry under the temporary directory.
    fs::path MakeSubPath(const fs::path& subpath) const { return path / subpath.relative_path(); }

    // Create an empty directory under the temporary directory.
    bool MakeSubDir(const fs::path& subdir) const {
        const fs::path path = MakeSubPath(subdir);
        if (auto s = base::file::mkdir_recursive(path, 0755); !s.ok()) {
            LOG(ERROR) << "Can't create " << path << " - " << s;
            return false;
        }
        if (!base::file::exists(path)) {
            LOG(WARNING) << "Created Path (" << path << ") does not exist";
        }
        VLOG(1) << "Created " << path;
        return true;
    }

    // Create an empty file under the temporary directory.
    bool MakeSubFile(std::string_view file) const {
        const fs::path path = MakeSubPath(file);
        const std::ofstream f(path);
        return true;
    }

  private:
#ifdef _WIN32
    static fs::path GetTempPath() {
        std::string result;
        DWORD len = ::GetTempPathA(0, NULL);
        if (!len) {
            LOG(FATAL) << "Can't find temporary path!";
        }
        result.resize(static_cast<size_t>(len));
        ::GetTempPathA(len, &result[0]);
        // The length returned by GetTempPath() is sometimes too large.
        result.resize(::strlen(result.c_str()));
        return fs::path(result);
    }
#else  // !_WIN32
    static fs::path GetTempPath() {
        fs::path result;
        // Only check TMPDIR if we're not root.
        if (getuid() != 0 && getgid() != 0) {
            const char* tmpdir = ::getenv("TMPDIR");
            if (tmpdir && tmpdir[0]) {
                result = tmpdir;
            }
        }
        // Otherwise use P_tmpdir, which defaults to /tmp
        if (result.empty()) {
#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif
            result = fs::path(P_tmpdir);
        }
        // Check that it exists and is a directory.
        if (!base::file::exists(result) || !base::file::is_dir(result)) {
            LOG(FATAL) << "Can't find temporary path: [" << result.c_str() << "]";
        }
        return result;
    }
#endif  // !_WIN32

  public:
    static std::string GenerateRandomString(int length = 8) {
        // Define allowed characters
        const std::string allowed_chars =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

        // Set up a high-quality random number generator
        std::random_device rd;   // Used to obtain a seed for the random engine
        std::mt19937 gen(rd());  // Standard mersenne_twister_engine seeded with rd()
        std::uniform_int_distribution<> distrib(0, static_cast<int>(allowed_chars.size()) - 1);

        // Generate the random string
        std::string random_str;
        random_str.reserve(length);  // Pre-allocate space for efficiency
        for (int i = 0; i < length; ++i) {
            random_str += allowed_chars[distrib(gen)];
        }

        return random_str;
    }

    fs::path path;
};

}  // namespace android::base
