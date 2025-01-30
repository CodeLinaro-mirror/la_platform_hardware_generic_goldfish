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

#include "android/base/system/System.h"

#include <fcntl.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "absl/log/log.h"

#include "aemu/base/EintrWrapper.h"
#include "aemu/base/files/PathUtils.h"
#include "aemu/base/misc/FileUtils.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
#define chmod _wchmod
#endif

#define ARRAYLEN(x) (sizeof(x) / sizeof((x)[0]))

namespace android {
namespace base {

static void make_subfile(fs::path dir, fs::path file) {
    fs::path path = dir / file.relative_path();
    int fd = ::open(System::pathAsString(path).c_str(), O_WRONLY | O_CREAT, 0755);
    EXPECT_GE(fd, 0) << "Path: " << path.c_str();
    LOG(INFO) << "Created: " << path;
    ::close(fd);
}

static void make_sized_file(fs::path dir, std::string file, size_t nBytes) {
    fs::path path = dir / file;
    int fd = ::open(System::pathAsString(path).c_str(), O_WRONLY | O_CREAT, 0755);
    EXPECT_GE(fd, 0) << "Unable to create file: " << path;
    setFileSize(fd, nBytes);
    ::close(fd);

    fd = ::open(System::pathAsString(path).c_str(), O_RDONLY);
    EXPECT_EQ(nBytes, System::get()->fileSize(fd)->bytes())
            << "File size of:" << path << " is not correct.";
    ::close(fd);
}

TEST(System, get) {
    System* sys1 = System::get();
    EXPECT_TRUE(sys1);

    System* sys2 = System::get();
    EXPECT_EQ(sys1, sys2);
}

TEST(System, getProgramDirectory) {
    std::string dir = System::pathAsString(System::get()->getProgramDirectory());
    EXPECT_FALSE(dir.empty());
    LOG(INFO) << "Program directory: [" << dir.c_str() << "]";
}

TEST(System, getHomeDirectory) {
    std::string dir = System::pathAsString(System::get()->getHomeDirectory());
    EXPECT_FALSE(dir.empty());
    LOG(INFO) << "Home directory: [" << dir.c_str() << "]";
}

TEST(System, getAppDataDirectory) {
    std::string dir = System::pathAsString(System::get()->getAppDataDirectory());
#if defined(__linux__)
    EXPECT_TRUE(dir.empty());
#else
    // Mac OS X, Microsoft Windows
    EXPECT_FALSE(dir.empty());
#endif  // __linux__
    LOG(INFO) << "AppData directory: [" << dir.c_str() << "]";
}

TEST(System, getCurrentDirectory) {
    std::string dir = System::pathAsString(System::get()->getCurrentDirectory());
    EXPECT_FALSE(dir.empty());
    LOG(INFO) << "Current directory: [" << dir.c_str() << "]";
}

// Tests case where program directory == launcher directory (QEMU1)
TEST(TestSystem, getDirectory) {
    const char kLauncherDir[] = "/foo/bar";
    const char kHomeDir[] = "/mama/papa";
#if defined(__linux__)
    const char* kAppDataDir = "";
#else
    // Mac OS X, Microsoft Windows
    const char kAppDataDir[] = "/lala/kaka";
#endif  // __linux__
    TestSystem testSys(kLauncherDir, kHomeDir, kAppDataDir);
    std::string ldir = System::pathAsString(System::get()->getLauncherDirectory());
    EXPECT_STREQ(kLauncherDir, ldir.c_str());
    std::string pdir = System::pathAsString(System::get()->getProgramDirectory());
    EXPECT_STREQ(kLauncherDir, pdir.c_str());
    std::string hdir = System::pathAsString(System::get()->getHomeDirectory());
    EXPECT_STREQ(kHomeDir, hdir.c_str());
    std::string adir = System::pathAsString(System::get()->getAppDataDirectory());
#if defined(__linux__)
    EXPECT_TRUE(adir.empty());
#else
    // Mac OS X, Microsoft Windows
    EXPECT_STREQ(kAppDataDir, adir.c_str());
#endif  // __linux__
}

// Tests case where program directory is a subdirectory of launcher directory
// (QEMU2)
TEST(TestSystem, getDirectoryProgramDir) {
    const char kLauncherDir[] = "/foo/bar";
    const char kProgramDir[] = "qemu/os-arch";
    TestSystem testSys(kLauncherDir, "/home", "/app");
    testSys.setProgramSubDir(kProgramDir);

    std::string ldir = System::pathAsString(System::get()->getLauncherDirectory());
    EXPECT_STREQ(kLauncherDir, ldir.c_str());
    std::string pdir = System::pathAsString(System::get()->getProgramDirectory());
#ifdef _WIN32
    EXPECT_STREQ("/foo/bar\\qemu/os-arch", pdir.c_str());
#else
    EXPECT_STREQ("/foo/bar/qemu/os-arch", pdir.c_str());
#endif
}

TEST(System, granularity) {
    auto start = System::get()->getUnixTimeUs();
    auto now = start;
    auto until = now + 1000 * 1000;
    int diff = 0, cnt = 0;

    while (now < until) {
        // Time should increase..
        auto nxt = System::get()->getUnixTimeUs();
        if (nxt != now) {
            diff++;
        }
        now = nxt;
    }
    float resolution = (float)(now - start) / diff;
    LOG(INFO) << "Counted " << diff << " slices in " << (now - start) << " us, a slice is +/- "
              << resolution << " us.";
    EXPECT_GT(diff, 1000);
}

TEST(System, getProgramBitness) {
    const int kExpected = (sizeof(void*) == 8) ? 64 : 32;
    EXPECT_EQ(kExpected, System::get()->getProgramBitness());
}

TEST(System, getOsName) {
    std::string osName = System::get()->getOsName();
    LOG(INFO) << "Host OS: " << osName;
    EXPECT_STRNE("Error: ", osName.substr(0, 7).c_str());
}

TEST(System, scandDirEntries) {
    static const char* const kExpected[] = {"fifth", "first", "fourth", "second", "sixth", "third"};
    static const char* const kInput[] = {"first", "second", "third", "fourth", "fifth", "sixth"};
    const size_t kCount = ARRAYLEN(kInput);

    TestTempDir myDir("scanDirEntries");
    for (size_t n = 0; n < kCount; ++n) {
        make_subfile(myDir.path(), kInput[n]);
    }

    auto entries = System::get()->scanDirEntries(myDir.path());

    EXPECT_EQ(kCount, entries.size());
    for (size_t n = 0; n < kCount; ++n) {
        EXPECT_STREQ(kExpected[n], System::pathAsString(entries[n]).c_str()) << "#" << n;
    }
}

TEST(System, recursiveSize) {
    static const char* const kDirs[] = {"d1", "d2", "d2/d2a", "d2/d2b"};

    static const char* const kFiles[] = {
            "f1",      "f2",      "f3",      "d1/d1f1", "d1/d1f2", "d1/d1f3",      "d1/d1f4",
            "d2/d2f1", "d2/d2f2", "d2/d2f3", "d2/d2f4", "d2/d2f5", "d2/d2a/d2af1", "d2/d2a/d2af2",
            // (d2/d2b is empty)
    };
    static const System::FileSize kFileSizes[] = {123,  55,    2345,   2222,   3333,    8329, 472,
                                                  4384, 54793, 234454, 113432, 4883232, 93,   834};

    // Create the directories
    TestSystem testSys("/foo/bar");
    TestTempDir* myDir = testSys.getTempRoot();
    size_t nItems = ARRAYLEN(kDirs);
    for (size_t idx = 0; idx < nItems; idx++) {
        EXPECT_TRUE(myDir->makeSubDir(kDirs[idx]));
    }

    // Write files into the directories
    System::FileSize expectedTotalSize = 0;
    nItems = ARRAYLEN(kFiles);
    for (size_t idx = 0; idx < nItems; idx++) {
        make_sized_file(myDir->path(), kFiles[idx], kFileSizes[idx].bytes());
        expectedTotalSize += kFileSizes[idx];
    }

    EXPECT_EQ(expectedTotalSize, System::get()->recursiveSize(myDir->path()));

    // Test an individual file
    EXPECT_EQ(kFileSizes[0], System::get()->recursiveSize(myDir->path() / kFiles[0]));
}

TEST(System, envGetAndSet) {
    System* sys = System::get();
    const char kVarName[] = "FOO_BAR_TESTING_STUFF";
    const char kVarValue[] = "SomethingCompletelyRandomForYou!";

    EXPECT_FALSE(sys->envTest(kVarName));
    EXPECT_STREQ("", sys->envGet(kVarName).c_str());
    sys->envSet(kVarName, kVarValue);
    EXPECT_TRUE(sys->envTest(kVarName));
    EXPECT_STREQ(kVarValue, sys->envGet(kVarName).c_str());
    sys->envSet(kVarName, nullptr);
    EXPECT_FALSE(sys->envTest(kVarName));
    EXPECT_STREQ("", sys->envGet(kVarName).c_str());
}

TEST(System, pathIsDir) {
    TestSystem sys("/bin", "/");

    EXPECT_FALSE(sys.pathIsDir("foo"));
    EXPECT_FALSE(sys.pathIsDir("foo/"));
#ifdef _WIN32
    EXPECT_FALSE(sys.pathIsDir("foo\\"));
#endif

    EXPECT_TRUE(sys.getTempRoot()->makeSubDir("foo"));

    EXPECT_TRUE(sys.pathIsDir("foo"));
    EXPECT_TRUE(sys.pathIsDir("foo/"));
#ifdef _WIN32
    EXPECT_TRUE(sys.pathIsDir("foo\\"));
#endif
}

#ifdef _MSC_VER
TEST(System, DISABLED_pathOperations) {
#else
TEST(System, pathOperations) {
#endif
    System* sys = System::get();
    TestTempDir tempDir("path_opts");
    auto fooPath = tempDir.path() / "foo";
    System::FileSize fileSize;

    EXPECT_FALSE(sys->pathExists(fooPath));
    EXPECT_FALSE(sys->pathIsFile(fooPath));
    EXPECT_FALSE(sys->pathIsDir(fooPath));
    EXPECT_FALSE(sys->pathCanRead(fooPath));
    EXPECT_FALSE(sys->pathCanWrite(fooPath));
    EXPECT_FALSE(sys->pathCanExec(fooPath));
    EXPECT_FALSE(sys->pathFileSize(fooPath, &fileSize));

    const auto createTimeBefore = sys->getUnixTimeUs();
    make_subfile(tempDir.path(), "foo");
    const auto createTimeAfter = sys->getUnixTimeUs();

    EXPECT_TRUE(sys->pathExists(fooPath));
    EXPECT_TRUE(sys->pathIsFile(fooPath));
    EXPECT_FALSE(sys->pathIsDir(fooPath));

    // NOTE: Windows doesn't have 'execute' permission bits.
    // Any readable file can be executed. Also any writable file
    // is readable.
    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.c_str(), S_IRUSR | S_IWUSR | S_IXUSR)));
    EXPECT_TRUE(sys->pathCanRead(fooPath));
    EXPECT_TRUE(sys->pathCanWrite(fooPath));
    EXPECT_TRUE(sys->pathCanExec(fooPath));

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.c_str(), S_IRUSR)));
    EXPECT_TRUE(sys->pathCanRead(fooPath));
    EXPECT_FALSE(sys->pathCanWrite(fooPath));
#ifdef _WIN32
    EXPECT_TRUE(sys->pathCanExec(fooPath));
#else
    EXPECT_FALSE(sys->pathCanExec(fooPath));
#endif

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.c_str(), S_IWUSR)));
#ifdef _WIN32
    EXPECT_TRUE(sys->pathCanRead(fooPath));
    EXPECT_TRUE(sys->pathCanWrite(fooPath));
    EXPECT_TRUE(sys->pathCanExec(fooPath));
#else
    EXPECT_FALSE(sys->pathCanRead(fooPath));
    EXPECT_TRUE(sys->pathCanWrite(fooPath));
    EXPECT_FALSE(sys->pathCanExec(fooPath));
#endif

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.c_str(), S_IXUSR)));
#ifdef _WIN32
    EXPECT_TRUE(sys->pathCanRead(fooPath));
#else
    EXPECT_FALSE(sys->pathCanRead(fooPath));
#endif
    EXPECT_FALSE(sys->pathCanWrite(fooPath));
    EXPECT_TRUE(sys->pathCanExec(fooPath));

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.c_str(), S_IRUSR | S_IWUSR)));
    EXPECT_TRUE(sys->pathCanRead(fooPath));
    EXPECT_TRUE(sys->pathCanWrite(fooPath));
#ifdef _WIN32
    EXPECT_TRUE(sys->pathCanExec(fooPath));
#else
    EXPECT_FALSE(sys->pathCanExec(fooPath));
#endif

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.c_str(), S_IRUSR | S_IXUSR)));
    EXPECT_TRUE(sys->pathCanRead(fooPath));
    EXPECT_FALSE(sys->pathCanWrite(fooPath));
    EXPECT_TRUE(sys->pathCanExec(fooPath));

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.c_str(), S_IWUSR | S_IXUSR)));
#ifdef _WIN32
    EXPECT_TRUE(sys->pathCanRead(fooPath));
#else
    EXPECT_FALSE(sys->pathCanRead(fooPath));
#endif
    EXPECT_TRUE(sys->pathCanWrite(fooPath));
    EXPECT_TRUE(sys->pathCanExec(fooPath));

    EXPECT_FALSE(sys->pathFileSize(fooPath, nullptr));
    EXPECT_TRUE(sys->pathFileSize(fooPath, &fileSize));
    EXPECT_EQ(0U, fileSize.bytes());

    std::ofstream fooFile(fooPath);
    ASSERT_TRUE(bool(fooFile));
    fooFile << "Some non-zero data";
    fooFile.close();
    EXPECT_TRUE(sys->pathFileSize(fooPath, &fileSize));
    EXPECT_LT(0U, fileSize.bytes());

    // Test creation time getter.
    auto createTime = sys->pathCreationTime(fooPath);
#ifdef __linux__
    EXPECT_FALSE(createTime);
    // Just to make the variables used.
    EXPECT_TRUE(createTimeBefore <= createTimeAfter);
#else
    ASSERT_TRUE(createTime);
    // On Windows creation time only contains seconds, so we need to make sure
    // we're comparing with the right precision.
    EXPECT_GE(*createTime, createTimeBefore - (createTimeBefore % 1000000));
    EXPECT_LE(*createTime, createTimeAfter);
#endif
    // Test file deletion
    EXPECT_TRUE(sys->deleteFile(fooPath));
    EXPECT_FALSE(sys->pathFileSize(fooPath, &fileSize));
}

TEST(System, scanDirEntriesWithFullPaths) {
    static const char* const kExpected[] = {"fifth", "first", "fourth", "second", "sixth", "third"};
    static const char* const kInput[] = {"first", "second", "third", "fourth", "fifth", "sixth"};
    const size_t kCount = ARRAYLEN(kInput);

    TestTempDir myDir("scanDirEntriesFull");
    for (size_t n = 0; n < kCount; ++n) {
        make_subfile(myDir.path(), kInput[n]);
    }

    auto entries = System::get()->scanDirEntries(myDir.path(), true);

    EXPECT_EQ(kCount, entries.size());
    for (size_t n = 0; n < kCount; ++n) {
        std::string expected(System::pathAsString(myDir.path()));
        expected = PathUtils::addTrailingDirSeparator(expected);
        expected += kExpected[n];
        EXPECT_STREQ(expected.c_str(), System::pathAsString(entries[n]).c_str()) << "#" << n;
    }
}

TEST(System, isRemoteSession) {
    std::string sessionType;
    bool isRemote = System::get()->isRemoteSession(&sessionType);
    if (isRemote) {
        LOG(INFO) << "Remote session type [" << sessionType.c_str() << "]";
    } else {
        LOG(INFO) << "Local session type";
    }
}

TEST(System, addLibrarySearchDir) {
    TestSystem testSys("/foo/bar");
    TestTempDir* testDir = testSys.getTempRoot();
    ASSERT_TRUE(testDir->makeSubDir("lib"));
    testSys.addLibrarySearchDir("lib");
}

TEST(System, findBundledExecutable) {
#ifdef _WIN32
    static const char kProgramFile[] = "myprogram.exe";
#else
    static const char kProgramFile[] = "myprogram";
#endif

    TestSystem testSys("/foo");
    TestTempDir* testDir = testSys.getTempRoot();
    ASSERT_TRUE(testDir->makeSubDir("foo"));

    fs::path path = "/foo";
    path /= System::kBinSubDir;
    ASSERT_TRUE(testDir->makeSubDir(path));

    auto programPath = path / kProgramFile;
    make_subfile(testDir->path(), programPath);
    ASSERT_TRUE(testSys.pathIsFile(programPath));
    LOG(INFO) << "Using launch dir: " << testSys.getLauncherDirectory();

    path = testSys.findBundledExecutable("myprogram");
    EXPECT_EQ(programPath, path);

    path = testSys.findBundledExecutable("otherprogram");
    EXPECT_TRUE(path.empty());
}

TEST(System, getProcessTimes) {
    const System::Times times1 = System::get()->getProcessTimes();
    const System::Times times2 = System::get()->getProcessTimes();
    ASSERT_GE(times2.userMs, times1.userMs);
    ASSERT_GE(times2.systemMs, times1.systemMs);
}

TEST(System, getUnixTime) {
    const time_t curTime = time(nullptr);
    const time_t time1 = System::get()->getUnixTime();
    const time_t time2 = System::get()->getUnixTime();
    ASSERT_GE(time1, curTime);
    ASSERT_GE(time2, time1);
}

}  // namespace base
}  // namespace android
