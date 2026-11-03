// Copyright 2019 The Android Open Source Project
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
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "android/base/testing/test_temp_dir.h"
#include "android/crashreport/crash_system.h"
#include "android/process/command.h"
#include "client/crash_report_database.h"
#include "client/settings.h"

using android::base::Bazel;
using android::base::Command;
using android::base::System;
using android::base::TestTempDir;
using crashpad::CrashReportDatabase;

namespace fs = std::filesystem;

const std::string kCrashMe = "crash-me";
using android::crashreport::CrashSystem;

class CrashTest : public ::testing::Test {
  protected:
    void SetUp() override {
#ifdef _WIN32
        // TODO(whollins,b/449212254): Fix this.
        GTEST_SKIP() << "crash report generation is currently broken on windows";
#endif
        absl::SetVLogLevel("*", 1);
        mTempDir = std::make_unique<TestTempDir>("crash_test_db");
        mDbPath = mTempDir->Path() / "测试_🚀_é" / "db";
        std::filesystem::create_directories(mDbPath);
        System::Get()->EnvSet(std::string(android::crashreport::kCrashDatabaseEnvVar),
                              mDbPath.string());
        mCrashdatabase = InitializeCrashDatabase();
        ASSERT_NE(mCrashdatabase, nullptr);
        deleteReports();
    }

    void TearDown() override {
        deleteReports();
        mCrashdatabase.reset();
        mTempDir.reset();
        System::Get()->EnvSet(std::string(android::crashreport::kCrashDatabaseEnvVar), "");
    }

    std::vector<CrashReportDatabase::Report> getAllReports() {
        if (!mCrashdatabase) {
            return {};
        }
        std::vector<CrashReportDatabase::Report> reports;
        std::vector<CrashReportDatabase::Report> pendingReports;
        mCrashdatabase->GetCompletedReports(&reports);
        mCrashdatabase->GetPendingReports(&pendingReports);
        reports.insert(reports.end(), pendingReports.begin(), pendingReports.end());
        return reports;
    }

    void deleteReports() {
        for (const auto& report : getAllReports()) {
            LOG(INFO) << "Deleting crash report: " << report.uuid.ToString();
            mCrashdatabase->DeleteReport(report.uuid);
        }
    }

    std::unique_ptr<CrashReportDatabase> InitializeCrashDatabase() {
        if (Bazel::InBazel()) {
            auto crashpad_handler = Bazel::RunfilesPath("crashpad+/handler/crashpad_handler");
            LOG(INFO) << "Using " << android::crashreport::kCrashpadHandlerEnvVar << ":"
                      << crashpad_handler;
            System::Get()->EnvSet(std::string(android::crashreport::kCrashpadHandlerEnvVar),
                                  crashpad_handler);
        }

        auto database_path = CrashSystem::DatabaseDirectory();
        auto crashDatabase =
                crashpad::CrashReportDatabase::Initialize(base::FilePath(database_path));
        if (crashDatabase && crashDatabase->GetSettings()) {
            crashDatabase->GetSettings()->SetUploadsEnabled(false);
        }

        return crashDatabase;
    }

    void crash(const std::vector<std::string>& extra_args = {}) {
        fs::path executable = Bazel::RunfilesPath("goldfish+/emulator/crashreport/crash-me");
        if (!Bazel::InBazel()) {
            GTEST_SKIP() << "This test can only be run under Bazel";
        }
        std::vector<std::string> args = {executable.string(), "--delay_ms", "0"};
        args.insert(args.end(), extra_args.begin(), extra_args.end());
        std::basic_stringbuf<char> std_err;
        auto proc = Command::Create(args).Inherit().RedirectStderrToUnsafe(&std_err).Execute();
        proc->WaitFor(std::chrono::seconds(10));
        auto res = proc->Err()->AsString();
        if (!res.empty()) LOG(INFO) << "Crash results: " << res;
    }

    bool waitForNewReportCount(size_t before) {
        auto deadline = absl::Now() + absl::Seconds(15);
        while (absl::Now() < deadline) {
            absl::SleepFor(absl::Milliseconds(100));
            const size_t after = getAllReports().size();
            if (after > before) {
                return true;
            }
        }
        return false;
    }

    std::unique_ptr<TestTempDir> mTempDir;
    fs::path mDbPath;
    std::unique_ptr<CrashReportDatabase> mCrashdatabase;
};

TEST_F(CrashTest, crash_generates_minidump) {
    const size_t before = getAllReports().size();
    LOG(INFO) << "Starting of with " << before << " crashes in the db";
    crash();

    EXPECT_TRUE(waitForNewReportCount(before))
            << "The database should have recorded an additional crash!";
}
