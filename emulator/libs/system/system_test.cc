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

#include <algorithm>
#include <cstddef>
#include <cstdlib>
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
#include <windows.h>
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
    const char kVarValue1[] = "SomethingCompletelyRandomForYou!";
    const char kVarValue2[] = "OverwrittenValue!";
    const std::string kVarPrefix = std::string(kVarName) + "=";

    auto isInEnvGetAll = [&](std::string_view expected_entry) {
        const auto all = sys->EnvGetAll();
        return std::find(all.begin(), all.end(), expected_entry) != all.end();
    };
    auto hasPrefixInEnvGetAll = [&]() {
        const auto all = sys->EnvGetAll();
        return std::any_of(all.begin(), all.end(),
                           [&](const std::string& entry) { return entry.starts_with(kVarPrefix); });
    };

    sys->EnvSet(kVarName, nullptr);
    EXPECT_FALSE(sys->EnvTest(kVarName));
    EXPECT_STREQ("", sys->EnvGet(kVarName).c_str());
    EXPECT_EQ(std::getenv(kVarName), nullptr);
    EXPECT_FALSE(hasPrefixInEnvGetAll());

    // Set initial value.
    sys->EnvSet(kVarName, kVarValue1);
    EXPECT_TRUE(sys->EnvTest(kVarName));
    EXPECT_STREQ(kVarValue1, sys->EnvGet(kVarName).c_str());
    ASSERT_NE(std::getenv(kVarName), nullptr);
    EXPECT_STREQ(kVarValue1, std::getenv(kVarName));
    EXPECT_TRUE(isInEnvGetAll(kVarPrefix + kVarValue1));

    // Overwrite existing variable with a new value.
    sys->EnvSet(std::string(kVarName), std::string(kVarValue2));
    EXPECT_TRUE(sys->EnvTest(kVarName));
    EXPECT_STREQ(kVarValue2, sys->EnvGet(kVarName).c_str());
    ASSERT_NE(std::getenv(kVarName), nullptr);
    EXPECT_STREQ(kVarValue2, std::getenv(kVarName));
    EXPECT_TRUE(isInEnvGetAll(kVarPrefix + kVarValue2));

    // Unset via empty std::string and verify full removal from environment.
    sys->EnvSet(std::string(kVarName), std::string(""));
    EXPECT_FALSE(sys->EnvTest(kVarName));
    EXPECT_STREQ("", sys->EnvGet(kVarName).c_str());
    EXPECT_EQ(std::getenv(kVarName), nullptr);
    EXPECT_FALSE(hasPrefixInEnvGetAll());

    // Set again and unset via nullptr.
    sys->EnvSet(kVarName, kVarValue1);
    EXPECT_TRUE(sys->EnvTest(kVarName));
    sys->EnvSet(kVarName, nullptr);
    EXPECT_FALSE(sys->EnvTest(kVarName));
    EXPECT_STREQ("", sys->EnvGet(kVarName).c_str());
    EXPECT_EQ(std::getenv(kVarName), nullptr);
    EXPECT_FALSE(hasPrefixInEnvGetAll());

    // Empty variable names should be safely ignored without UB.
    System::SetEnvironmentVariable("", kVarValue1);
    System::SetEnvironmentVariable("", "");
    EXPECT_EQ(System::GetEnvironmentVariable(""), "");
    EXPECT_FALSE(sys->EnvTest(""));
}

TEST(System, staticGetAndSetEnvironmentVariable) {
    constexpr std::string_view kVarName = "FOO_BAR_STATIC_ENV_TESTING";
    constexpr std::string_view kVarValue = "StaticEnvValue";
    constexpr std::string_view kUtf8Value = "T\xC3\xA9l\xC3\xA9vision";
    constexpr std::string_view kNameBuffer = "FOO_BAR_STATIC_ENV_TESTING_SUFFIX";
    constexpr std::string_view kValueBuffer = "StaticEnvValue_SUFFIX";
    const std::string_view sliced_name = kNameBuffer.substr(0, kVarName.size());
    const std::string_view sliced_value = kValueBuffer.substr(0, kVarValue.size());

    System::SetEnvironmentVariable(sliced_name, "");
    EXPECT_EQ(System::GetEnvironmentVariable(sliced_name), "");
    EXPECT_FALSE(System::Get()->EnvTest(sliced_name));
    EXPECT_EQ(std::getenv(kVarName.data()), nullptr);

    // Set using non-NUL-terminated string_view slices.
    System::SetEnvironmentVariable(sliced_name, sliced_value);
    EXPECT_EQ(System::GetEnvironmentVariable(kVarName), kVarValue);
    EXPECT_EQ(System::GetEnvironmentVariable(sliced_name), kVarValue);
    EXPECT_EQ(System::GetEnvironmentVariable(kNameBuffer), "");
    EXPECT_TRUE(System::Get()->EnvTest(sliced_name));
    EXPECT_FALSE(System::Get()->EnvTest(kNameBuffer));
    ASSERT_NE(std::getenv(kVarName.data()), nullptr);
    EXPECT_EQ(std::string_view(std::getenv(kVarName.data())), kVarValue);
#ifdef _WIN32
    {
        char win_buf[64] = {};
        const DWORD len = ::GetEnvironmentVariableA(kVarName.data(), win_buf, sizeof(win_buf));
        EXPECT_EQ(len, kVarValue.size());
        EXPECT_EQ(std::string_view(win_buf, len), kVarValue);
    }
#endif

    // Overwrite with a UTF-8 non-ASCII value.
    System::SetEnvironmentVariable(sliced_name, kUtf8Value);
    EXPECT_EQ(System::GetEnvironmentVariable(kVarName), kUtf8Value);
    EXPECT_EQ(System::GetEnvironmentVariable(sliced_name), kUtf8Value);
    ASSERT_NE(std::getenv(kVarName.data()), nullptr);
    EXPECT_EQ(std::string_view(std::getenv(kVarName.data())), kUtf8Value);
#ifdef _WIN32
    // The emulator only supports Windows 11 onwards, and setting the UTF-8
    // code page via the application manifest (<activeCodePage>UTF-8</activeCodePage>)
    // is supported starting from Windows 10 version 1903 (build 10.0.18362).
    // However, there are still some Windows CI build bots running Windows
    // Server 2019 (version 1809, build 10.0.17763) where the manifest setting
    // is ignored and ::GetACP() falls back to the legacy ANSI code page.
    if (System::IsUtf8CodePage()) {
        wchar_t wbuf[64] = {};
        const DWORD wlen = ::GetEnvironmentVariableW(L"FOO_BAR_STATIC_ENV_TESTING", wbuf, 64);
        EXPECT_EQ(std::wstring_view(wbuf, wlen), L"T\u00E9l\u00E9vision");
    }
#endif

    // Unset via empty string_view and verify removal in both CRT and Win32.
    System::SetEnvironmentVariable(sliced_name, "");
    EXPECT_EQ(System::GetEnvironmentVariable(kVarName), "");
    EXPECT_FALSE(System::Get()->EnvTest(sliced_name));
    EXPECT_EQ(std::getenv(kVarName.data()), nullptr);
#ifdef _WIN32
    {
        char win_buf[16] = {};
        EXPECT_EQ(::GetEnvironmentVariableA(kVarName.data(), win_buf, sizeof(win_buf)), 0u);
        EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_ENVVAR_NOT_FOUND));
    }
#endif
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
