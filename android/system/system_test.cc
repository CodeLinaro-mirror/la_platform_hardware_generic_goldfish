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

#include "android/base/system.h"

#include <fcntl.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "absl/log/log.h"

#include "aemu/base/EintrWrapper.h"
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

TEST(System, get) {
    System* sys1 = System::get();
    EXPECT_TRUE(sys1);

    System* sys2 = System::get();
    EXPECT_EQ(sys1, sys2);
}

TEST(System, getHomeDirectory) {
    std::string dir = System::get()->getHomeDirectory().string();
    EXPECT_FALSE(dir.empty());
    LOG(INFO) << "Home directory: [" << dir.c_str() << "]";
}

TEST(System, getAppDataDirectory) {
    std::string dir = System::get()->getAppDataDirectory().string();
#if defined(__linux__)
    EXPECT_TRUE(dir.empty());
#else
    // Mac OS X, Microsoft Windows
    EXPECT_FALSE(dir.empty());
#endif  // __linux__
    LOG(INFO) << "AppData directory: [" << dir.c_str() << "]";
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
