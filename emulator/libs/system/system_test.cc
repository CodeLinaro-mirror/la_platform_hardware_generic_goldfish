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

#include "android/base/eintr_wrapper.h"
#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
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
    System* sys1 = System::Get();
    EXPECT_TRUE(sys1);

    System* sys2 = System::Get();
    EXPECT_EQ(sys1, sys2);
}

TEST(System, getHomeDirectory) {
    std::string dir = System::Get()->GetHomeDirectory().string();
    EXPECT_FALSE(dir.empty());
    LOG(INFO) << "Home directory: [" << dir.c_str() << "]";
}

TEST(System, getAppDataDirectory) {
    std::string dir = System::Get()->GetAppDataDirectory().string();
#if defined(__linux__)
    EXPECT_TRUE(dir.empty());
#else
    // Mac OS X, Microsoft Windows
    EXPECT_FALSE(dir.empty());
#endif  // __linux__
    LOG(INFO) << "AppData directory: [" << dir.c_str() << "]";
}

TEST(System, granularity) {
    auto start = System::Get()->GetUnixTimeUs();
    auto now = start;
    auto until = now + 1000 * 1000;
    int diff = 0;

    while (now < until) {
        // Time should increase..
        auto nxt = System::Get()->GetUnixTimeUs();
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
    EXPECT_EQ(kExpected, System::Get()->GetProgramBitness());
}

TEST(System, getOsName) {
    std::string osName = System::Get()->GetOsName();
    LOG(INFO) << "Host OS: " << osName;
    EXPECT_STRNE("Error: ", osName.substr(0, 7).c_str());
}

TEST(System, envGetAndSet) {
    System* sys = System::Get();
    const char kVarName[] = "FOO_BAR_TESTING_STUFF";
    const char kVarValue[] = "SomethingCompletelyRandomForYou!";

    EXPECT_FALSE(sys->EnvTest(kVarName));
    EXPECT_STREQ("", sys->EnvGet(kVarName).c_str());
    sys->EnvSet(kVarName, kVarValue);
    EXPECT_TRUE(sys->EnvTest(kVarName));
    EXPECT_STREQ(kVarValue, sys->EnvGet(kVarName).c_str());
    sys->EnvSet(kVarName, nullptr);
    EXPECT_FALSE(sys->EnvTest(kVarName));
    EXPECT_STREQ("", sys->EnvGet(kVarName).c_str());
}

TEST(System, isRemoteSession) {
    std::string session_type;
    bool isRemote = System::Get()->IsRemoteSession(&session_type);
    if (isRemote) {
        LOG(INFO) << "Remote session type [" << session_type.c_str() << "]";
    } else {
        LOG(INFO) << "Local session type";
    }
}

TEST(System, addLibrarySearchDir) {
    TestSystem testSys("/foo/bar");
    TestTempDir* testDir = testSys.GetTempRoot();
    ASSERT_TRUE(testDir->MakeSubDir("lib"));
    testSys.AddLibrarySearchDir("lib");
}

TEST(System, getProcessTimes) {
    const System::Times times1 = System::Get()->GetProcessTimes();
    const System::Times times2 = System::Get()->GetProcessTimes();
    ASSERT_GE(times2.user_ms, times1.user_ms);
    ASSERT_GE(times2.system_ms, times1.system_ms);
}

TEST(System, getUnixTime) {
    const time_t curTime = time(nullptr);
    const time_t time1 = System::Get()->GetUnixTime();
    const time_t time2 = System::Get()->GetUnixTime();
    ASSERT_GE(time1, curTime);
    ASSERT_GE(time2, time1);
}

}  // namespace base
}  // namespace android
