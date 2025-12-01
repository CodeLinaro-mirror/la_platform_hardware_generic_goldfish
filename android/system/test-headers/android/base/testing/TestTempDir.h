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

#include "aemu/base/Compiler.h"

#include "android/base/system/File.h"

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
namespace android {
namespace base {

// A class used to model a temporary directory used during testing.
// Usage is simple:
//
//      {
//        TestTempDir myDir("my_test");   // creates new temp directory.
//        ASSERT_TRUE(myDir.path());      // NULL if error during creation.
//        ... write files into directory path myDir->path()
//        ... do your test
//      }   // destructor removes temp directory and all files under it.

class TestTempDir {
  public:
    // Create new instance. This also tries to create a new temporary
    // directory. |debugPrefix| is an optional name prefix and can be empty.
    TestTempDir(std::string_view debugName) {
        // TODO use System::getTempDir() instead of getTempPath()
        mPath = getTempPath() / generate_random_string();
        if (!debugName.empty()) {
            mPath = fs::absolute(mPath / debugName);
        }

        if (fs::exists(mPath)) {
            base::file::rm_recursive(mPath);
        }
        // Attempt to create the temporary directory
        std::error_code ec;
        if (!fs::create_directories(mPath, ec)) {
            PLOG(WARNING) << "Failed to create " << mPath << " due to: " << ec;
        }
    }

    // Return the path to the temporary directory, or NULL if it could not
    // be created for some reason.
    fs::path path() const { return mPath; }

    // Return the path as a string. It will be empty if the directory could
    // not be created for some reason.
    const std::string pathString() const { return mPath.string(); }

    // Destroy instance, and removes the temporary directory and all files
    // inside it.
    ~TestTempDir() {
        if (!mPath.empty()) {
            base::file::rm_recursive(mPath);
        }
    }

    // Create the path of a directory entry under the temporary directory.
    fs::path makeSubPath(fs::path subpath) { return mPath / subpath.relative_path(); }

    // Create an empty directory under the temporary directory.
    bool makeSubDir(fs::path subdir) {
        fs::path path = fs::absolute(makeSubPath(subdir));
        if (auto s = base::file::mkdir(path, 0755); !s.ok()) {
            LOG(ERROR) << "Can't create " << path << " - " << s;
            return false;
        }
        if (!base::file::exists(path)) {
            LOG(WARNING) << "Created path (" << path << "/" << subdir << ") does not exist";
        }
        VLOG(1) << "Created " << path;
        return true;
    }

    // Create an empty file under the temporary directory.
    bool makeSubFile(std::string_view file) {
        fs::path path = makeSubPath(file);
        std::ofstream f(path);
        return true;
    }

  private:
    DISALLOW_COPY_AND_ASSIGN(TestTempDir);

#ifdef _WIN32
    fs::path getTempPath() {
        std::string result;
        DWORD len = GetTempPathA(0, NULL);
        if (!len) {
            LOG(FATAL) << "Can't find temporary path!";
        }
        result.resize(static_cast<size_t>(len));
        GetTempPathA(len, &result[0]);
        // The length returned by GetTempPath() is sometimes too large.
        result.resize(::strlen(result.c_str()));
        return fs::path(result);
    }
#else  // !_WIN32
    fs::path getTempPath() {
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
    static std::string generate_random_string(int length = 8) {
        // Define allowed characters
        const std::string allowed_chars =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

        // Set up a high-quality random number generator
        std::random_device rd;   // Used to obtain a seed for the random engine
        std::mt19937 gen(rd());  // Standard mersenne_twister_engine seeded with rd()
        std::uniform_int_distribution<> distrib(0, allowed_chars.size() - 1);

        // Generate the random string
        std::string random_str;
        random_str.reserve(length);  // Pre-allocate space for efficiency
        for (int i = 0; i < length; ++i) {
            random_str += allowed_chars[distrib(gen)];
        }

        return random_str;
    }

    fs::path mPath;
};

}  // namespace base
}  // namespace android
