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

#include <aemu/base/files/PathUtils.h>

#include <filesystem>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "aemu/base/Compiler.h"
#include "android/base/file/file_io.h"
#include "android/base/system/System.h"

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#include <errno.h>
#include <stdio.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
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
        mPath = getTempPath();
        if (!debugName.empty()) {
            mPath = fs::absolute(mPath / debugName);
        }

        if (fs::exists(mPath)) {
            DeleteRecursive(mPath);
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
    const std::string pathString() const { return System::pathAsString(mPath); }

    // Destroy instance, and removes the temporary directory and all files
    // inside it.
    ~TestTempDir() {
        if (!mPath.empty()) {
            DeleteRecursive(mPath);
        }
    }

    // Create the path of a directory entry under the temporary directory.
    fs::path makeSubPath(fs::path subpath) { return mPath / subpath.relative_path(); }

    // Create an empty directory under the temporary directory.
    bool makeSubDir(fs::path subdir) {
        fs::path path = fs::absolute(makeSubPath(subdir));
        if (android_mkdir(path.string().c_str(), 0755) < 0) {
            LOG(ERROR) << "Can't create " << path;
            return false;
        }
        if (!pathExists(path.string().c_str())) {
            LOG(WARNING) << "Created path (" << path << "/" << subdir << ") does not exist";
        }
        VLOG(1) << "Created " << path;
        return true;
    }

    // Create an empty file under the temporary directory.
    bool makeSubFile(std::string_view file) {
        fs::path path = makeSubPath(file);
        int fd = ::android_open(System::pathAsString(path).c_str(), O_WRONLY | O_CREAT, 0744);
        if (fd < 0) {
            LOG(ERROR) << "Can't create" << path;
            return false;
        }
        ::close(fd);
        return true;
    }

  private:
    DISALLOW_COPY_AND_ASSIGN(TestTempDir);

    void DeleteRecursive(const fs::path& path) {
        if (!fs::exists(path)) {
            return;  // Path doesn't exist
        }

        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                DeleteRecursive(entry.path());  // Recursively delete subdirectories
            } else {
                LOG(INFO) << "Deleting " << path;
                android_unlink(System::pathAsString(entry.path()).c_str());
            }
        }

        VLOG(1) << "Rmdir " << path;
        android_rmdir(System::pathAsString(path).c_str());
    }

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
        for (size_t n = 0; n < result.size(); ++n) {
            if (result[n] == '\\') {
                result[n] = '/';
            }
        }
        if (result.size() && result[result.size() - 1] != '/') {
            result += '/';
        }
        return result;
    }

    char* mkdtemp(char* path) {
        char* sep = ::strrchr(path, '/');
        if (sep) {
            struct _stati64 st;
            int ret;
            *sep = '\0';  // temporarily zero-terminate the dirname.
            ret = android_stat(path, reinterpret_cast<struct stat*>(&st));
            *sep = '/';  // restore full path.
            if (ret < 0) {
                return NULL;
            }
            if (!S_ISDIR(st.st_mode)) {
                errno = ENOTDIR;
                return NULL;
            }
        }

        // Loop. On each iteration, replace the XXXXXX suffix with a random
        // number.
        char* path_end = path + ::strlen(path);
        const size_t kSuffixLen = 6U;
        for (int tries = 128; tries > 0; tries--) {
            int random = rand() % 1000000;

            snprintf(path_end - kSuffixLen, kSuffixLen + 1, "%0d", random);
            if (android_mkdir(path, 0755) == 0) {
                return path;  // Success
            }
            if (errno != EEXIST) {
                return NULL;
            }
        }
        return NULL;
    }
#else  // !_WIN32
    fs::path getTempPath() {
        std::string result;
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
            result = P_tmpdir;
        }
        // Check that it exists and is a directory.
        struct stat st;
        int ret = android_stat(result.c_str(), &st);
        if (ret < 0 || !S_ISDIR(st.st_mode)) {
            LOG(FATAL) << "Can't find temporary path: [" << result.c_str() << "]";
        }
        // Ensure there is a trailing directory separator.
        if (result.size() && result[result.size() - 1] != '/') {
            result += '/';
        }
        return result;
    }
#endif  // !_WIN32

    std::string generate_random_string(int length = 8) {
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
