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
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "android/base/system.h"
#include "android/system/test-headers/android/base/testing/TestTempDir.h"

namespace android {
namespace base {

namespace fs = std::filesystem;

// The TestSystem class provides a mock implementation that of the System that
// can be used by UnitTests. Instantiation will result in tempory replacement of
// the System singleton with this version, the prevous setting will be restored
// in the destructor of this object
//
// Some things to be aware of:
//
// Interaction with the filesystem will result in the creation of
// a temporary directory that will be used to interact with the
// filesystem. This temporary directory is deleted in the destructor.
//
// Path resolution is done as follows:
//   - Relative paths are resolved starting from current directory
//     which by default is: "/home"
//   - The construction of this object does not create the appDataDir
//     or home dir. If you need these directories to exist you will
//     have to create them as follows: getTempRoot()->makeSubDir("home").
//   - Path resolution can result in switching / into \ when running under
//      Win32. If you are doing anything with paths
//     it is best to include "android/utils/path.h" and use the PATH_SEP
//     and PATH_SEP_C macros to indicate path separators in your strings.
//
class TestSystem : public System {
  public:
    using System::GetEnvironmentVariable;
    using System::SetEnvironmentVariable;

    explicit TestSystem(fs::path ignored, fs::path homeDir = "/home", fs::path appDataDir = "")
            : mHomeDir(homeDir)
            , mAppDataDir(appDataDir)
            , mIsRemoteSession(false)
            , mRemoteSessionType()
            , mTempDir(std::make_unique<TestTempDir>("TestSystem"))
            , mEnvPairs()
            , mPrevSystem(System::SetForTesting(this))
            , mTimes()
            , mShellOpaque(nullptr)
            , mUnixTime() {}

    ~TestSystem() override { System::SetForTesting(mPrevSystem); }

    const fs::path GetHomeDirectory() const override { return mHomeDir; }

    void setHomeDirectory(const fs::path& homeDir) { mHomeDir = homeDir; }

    const fs::path GetAppDataDirectory() const override { return mAppDataDir; }

    void setAppDataDirectory(std::string_view appDataDir) { mAppDataDir = appDataDir; }

    OsType GetOsType() const override { return mOsType; }

    std::string GetOsName() override { return mOsName; }

    std::string GetMajorOsVersion() const override { return "0.0"; }

    int GetCpuCoreCount() const override { return mCoreCount; }

    void setCpuCoreCount(int count) { mCoreCount = count; }

    MemUsage GetMemUsage() const override {
        MemUsage res;
        res.resident = 4294967295ULL;
        res.resident_max = 4294967295ULL * 2;
        res.virt = 4294967295ULL * 4;
        res.virt_max = 4294967295ULL * 8;
        res.total_phys_memory = 4294967295ULL * 16;
        res.total_page_file = 4294967295ULL * 32;
        return res;
    }

    void setOsType(OsType type) { mOsType = type; }

    std::string EnvGet(std::string_view varname) const override {
        for (size_t n = 0; n < mEnvPairs.size(); n += 2) {
            const fs::path name = mEnvPairs[n];
            if (name == varname) {
                return mEnvPairs[n + 1];
            }
        }
        return std::string();
    }

    std::vector<std::string> EnvGetAll() const override {
        std::vector<std::string> res;
        for (size_t i = 0; i < mEnvPairs.size(); i += 2) {
            const std::string name = mEnvPairs[i];
            const std::string val = mEnvPairs[i + 1];
            res.push_back(name + "=" + val);
        }
        return res;
    }

    void EnvSet(const std::string& varname, const std::string& varvalue) override {
        // First, find if the name is in the array.
        int index = -1;
        for (size_t n = 0; n < mEnvPairs.size(); n += 2) {
            if (mEnvPairs[n] == varname) {
                index = static_cast<int>(n);
                break;
            }
        }
        if (varvalue.empty()) {
            // Remove definition, if any.
            if (index >= 0) {
                mEnvPairs.erase(mEnvPairs.begin() + index, mEnvPairs.begin() + index + 2);
            }
        } else {
            if (index >= 0) {
                // Replacement.
                mEnvPairs[index + 1] = varvalue;
            } else {
                // Addition.
                mEnvPairs.emplace_back(varname);
                mEnvPairs.emplace_back(varvalue);
            }
        }
    }

    bool EnvTest(std::string_view varname) const override {
        for (size_t n = 0; n < mEnvPairs.size(); n += 2) {
            const fs::path name = mEnvPairs[n];
            if (name == varname) {
                return true;
            }
        }
        return false;
    }

    TestTempDir* getTempRoot() const { return mTempDir.get(); }

    bool IsRemoteSession(std::string* sessionType) const override {
        if (!mIsRemoteSession) {
            return false;
        }
        *sessionType = mRemoteSessionType;
        return true;
    }

    // Force the remote session type. If |sessionType| is nullptr or empty,
    // this sets the session as local. Otherwise, |*sessionType| must be
    // a session type.
    void setRemoteSessionType(std::string_view sessionType) {
        mIsRemoteSession = !sessionType.empty();
        if (mIsRemoteSession) {
            mRemoteSessionType = sessionType;
        }
    }

    Times GetProcessTimes() const override { return mTimes; }

    void setProcessTimes(const Times& times) { mTimes = times; }

    // TODO remove.
    fs::path GetTempDir() const override { return "/tmp"; }

    bool GetEnableCrashReporting() const override { return true; }

    time_t GetUnixTime() const override { return GetUnixTimeUs() / 1000000; }

    Duration GetUnixTimeUs() const override { return GetHighResTimeUs(); }

    WallDuration GetHighResTimeUs() const override {
        if (mUnixTimeLive) {
            auto now = hostSystem()->GetHighResTimeUs();
            mUnixTime += now - mUnixTimeLastQueried;
            mUnixTimeLastQueried = now;
        }
        return mUnixTime;
    }

    void setUnixTime(time_t time) { setUnixTimeUs(time * 1000000LL); }

    void setUnixTimeUs(Duration time) { mUnixTimeLastQueried = mUnixTime = time; }

    void setLiveUnixTime(bool enable) {
        mUnixTimeLive = enable;
        if (enable) {
            mUnixTimeLastQueried = hostSystem()->GetHighResTimeUs();
        }
    }

    void ConfigureHost() const override {}

    System* host() { return hostSystem(); }

  private:
    fs::path mHomeDir;
    fs::path mAppDataDir;
    bool mIsRemoteSession;
    std::string mRemoteSessionType;
    std::unique_ptr<TestTempDir> mTempDir;
    std::vector<std::string> mEnvPairs;
    System* mPrevSystem;
    Times mTimes;
    void* mShellOpaque;
    mutable Duration mUnixTime;
    mutable Duration mUnixTimeLastQueried = 0;
    bool mUnixTimeLive = false;
    OsType mOsType = OsType::kWindows;
    std::string mOsName;
    bool mUnderWine = false;
    int mCoreCount = 4;
    std::optional<std::string> mWhich;
};

}  // namespace base
}  // namespace android
