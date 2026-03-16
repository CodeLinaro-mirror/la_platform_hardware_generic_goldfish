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
#include "absl/strings/str_cat.h"

#include "TestTempDir.h"
#include "android/base/system.h"

namespace android::base {

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
//   - The construction of this object does not create the app_data_dir
//     or home dir. If you need these directories to exist you will
//     have to create them as follows: GetTempRoot()->MakeSubDir("home").
//   - Path resolution can result in switching / into \ when running under
//      Win32. If you are doing anything with paths
//     it is best to include "android/utils/path.h" and use the PATH_SEP
//     and PATH_SEP_C macros to indicate path separators in your strings.
//
class TestSystem : public System {
  public:
    using System::GetEnvironmentVariable;
    using System::SetEnvironmentVariable;

    explicit TestSystem(const fs::path& /*ignored*/, fs::path home_dir = "/home",
                        fs::path app_data_dir = "")
            : home_dir_(std::move(home_dir))
            , app_data_dir_(std::move(app_data_dir))
            , temp_dir_(std::make_unique<TestTempDir>("TestSystem"))
            , prev_system_(System::SetForTesting(this))
            , times_() {}

    ~TestSystem() override { System::SetForTesting(prev_system_); }

    const fs::path GetHomeDirectory() const override { return home_dir_; }

    void SetHomeDirectory(const fs::path& home_dir) { home_dir_ = home_dir; }

    const fs::path GetAppDataDirectory() const override { return app_data_dir_; }

    void SetAppDataDirectory(std::string_view app_data_dir) { app_data_dir_ = app_data_dir; }

    OsType GetOsType() const override { return os_type_; }

    std::string GetOsName() override { return os_name_; }

    std::string GetMajorOsVersion() const override { return "0.0"; }

    int GetCpuCoreCount() const override { return core_count_; }

    void SetCpuCoreCount(int count) { core_count_ = count; }

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

    void SetOsType(OsType type) { os_type_ = type; }

    std::string EnvGet(std::string_view varname) const override {
        for (size_t n = 0; n < env_pairs_.size(); n += 2) {
            const fs::path name = env_pairs_[n];
            if (name == varname) {
                return env_pairs_[n + 1];
            }
        }
        return {};
    }

    std::vector<std::string> EnvGetAll() const override {
        std::vector<std::string> res;
        for (size_t i = 0; i < env_pairs_.size(); i += 2) {
            const std::string& name = env_pairs_[i];
            const std::string& val = env_pairs_[i + 1];
            res.push_back(absl::StrCat(name, "=", val));
        }
        return res;
    }

    void EnvSet(const std::string& varname, const std::string& varvalue) override {
        // First, find if the name is in the array.
        int index = -1;
        for (size_t n = 0; n < env_pairs_.size(); n += 2) {
            if (env_pairs_[n] == varname) {
                index = static_cast<int>(n);
                break;
            }
        }
        if (varvalue.empty()) {
            // Remove definition, if any.
            if (index >= 0) {
                env_pairs_.erase(env_pairs_.begin() + index, env_pairs_.begin() + index + 2);
            }
        } else {
            if (index >= 0) {
                // Replacement.
                env_pairs_[index + 1] = varvalue;
            } else {
                // Addition.
                env_pairs_.emplace_back(varname);
                env_pairs_.emplace_back(varvalue);
            }
        }
    }

    bool EnvTest(std::string_view varname) const override {
        for (size_t n = 0; n < env_pairs_.size(); n += 2) {
            const fs::path name = env_pairs_[n];
            if (name == varname) {
                return true;
            }
        }
        return false;
    }

    TestTempDir* GetTempRoot() const { return temp_dir_.get(); }

    bool IsRemoteSession(std::string* session_type) const override {
        if (!is_remote_session_) {
            return false;
        }
        *session_type = remote_session_type_;
        return true;
    }

    // Force the remote session type. If |session_type| is nullptr or empty,
    // this sets the session as local. Otherwise, |*session_type| must be
    // a session type.
    void SetRemoteSessionType(std::string_view session_type) {
        is_remote_session_ = !session_type.empty();
        if (is_remote_session_) {
            remote_session_type_ = session_type;
        }
    }

    Times GetProcessTimes() const override { return times_; }

    void SetProcessTimes(const Times& times) { times_ = times; }

    // TODO remove.
    fs::path GetTempDir() const override { return "/tmp"; }

    bool GetEnableCrashReporting() const override { return true; }

    time_t GetUnixTime() const override { return GetUnixTimeUs() / 1000000; }

    Duration GetUnixTimeUs() const override { return static_cast<Duration>(GetHighResTimeUs()); }

    WallDuration GetHighResTimeUs() const override {
        if (unix_time_live_) {
            auto now = hostSystem()->GetHighResTimeUs();
            unix_time_ += static_cast<Duration>(now - unix_time_last_queried_);
            unix_time_last_queried_ = now;
        }
        return static_cast<WallDuration>(unix_time_);
    }

    void SetUnixTime(time_t time) { SetUnixTimeUs(time * 1000000LL); }

    void SetUnixTimeUs(Duration time) {
        unix_time_ = time;
        unix_time_last_queried_ = hostSystem()->GetHighResTimeUs();
    }

    void SetLiveUnixTime(bool enable) {
        unix_time_live_ = enable;
        if (enable) {
            unix_time_last_queried_ = hostSystem()->GetHighResTimeUs();
        }
    }

    void ConfigureHost() const override {}

    static System* Host() { return hostSystem(); }

  private:
    fs::path home_dir_;
    fs::path app_data_dir_;
    bool is_remote_session_ = false;
    std::string remote_session_type_;
    std::unique_ptr<TestTempDir> temp_dir_;
    std::vector<std::string> env_pairs_;
    System* prev_system_;
    Times times_;
    mutable Duration unix_time_{};
    mutable WallDuration unix_time_last_queried_ = 0;
    bool unix_time_live_ = false;
    OsType os_type_ = OsType::kWindows;
    std::string os_name_;
    int core_count_ = 4;
    std::optional<std::string> which_;
};

}  // namespace android::base
