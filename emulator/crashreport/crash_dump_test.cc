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
#include <thread>

#include "absl/log/globals.h"
#include "absl/log/log.h"

#include "aemu/base/process/Command.h"
#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "android/crashreport/crash_initializer.h"
#include "client/crash_report_database.h"
#include "client/settings.h"
#include "crashpad/android/crashreport/crash_reporter.h"

using android::base::Command;
using android::base::System;
using crashpad::CrashReportDatabase;
using namespace std::chrono_literals;
using android::base::Bazel;

namespace fs = std::filesystem;
const constexpr char kCrashpadDatabase[] = "emu-dev-test-crash.db";

const std::string kCrashMe = "crash-me";
using android::crashreport::CrashReporter;

class CrashTest : public ::testing::Test {
  protected:
    void SetUp() override {
        absl::SetVLogLevel("*", 1);
        mCrashdatabase = InitializeCrashDatabase();
        deleteReports();
    }

    void TearDown() override {
        // deleteReports();
    }

    void deleteReports() {
        std::vector<crashpad::CrashReportDatabase::Report> reports;
        std::vector<crashpad::CrashReportDatabase::Report> pendingReports;
        mCrashdatabase->GetCompletedReports(&reports);
        mCrashdatabase->GetPendingReports(&pendingReports);
        reports.insert(reports.end(), pendingReports.begin(), pendingReports.end());

        for (const auto& report : reports) {
            LOG(INFO) << "Deleting crash report: " << report.uuid.ToString();
            mCrashdatabase->DeleteReport(report.uuid);
        }
    }

    std::unique_ptr<CrashReportDatabase> InitializeCrashDatabase() {
        if (Bazel::inBazel()) {
            auto crashpad_handler = Bazel::runfilesPath("crashpad+/handler/crashpad_handler");
            LOG(INFO) << "Using AEMU_CRASHPAD_HANDLER:" << crashpad_handler;
            System::setEnvironmentVariable("AEMU_CRASHPAD_HANDLER", crashpad_handler);
        }

        auto handler_path = CrashReporter::handlerExe();
        auto database_path = CrashReporter::databaseDirectory();
        auto crashDatabase =
                crashpad::CrashReportDatabase::Initialize(base::FilePath(database_path));
        crashDatabase->GetSettings()->SetUploadsEnabled(false);

        return crashDatabase;
    }

    void crash() {
        fs::path executable = Bazel::runfilesPath("goldfish+/emulator/crashreport/crash-me");
        if (!Bazel::inBazel()) {
            GTEST_SKIP() << "This test can only be run under Bazel";
        }
        auto proc = Command::create({executable.string(), "--delay_ms", "1000"})
                            .inherit()
                            .withStderrBuffer(4096, 10ms)
                            .execute();
        while (proc->isAlive()) {
            auto res = proc->err()->asString();
            if (!res.empty()) LOG(INFO) << "Crash results: " << res;
        }
    }

    std::unique_ptr<CrashReportDatabase> mCrashdatabase;
};

TEST_F(CrashTest, crash_generates_minidump) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "crash report generation is currently broken on windows";
#endif
    std::vector<CrashReportDatabase::Report> reports;
    std::vector<CrashReportDatabase::Report> pendingReports;
    mCrashdatabase->GetCompletedReports(&reports);
    mCrashdatabase->GetPendingReports(&pendingReports);

    auto before = reports.size() + pendingReports.size();

    LOG(INFO) << "Starting of with " << before << " crashes in the db";
    crash();

    int after = 0;
    auto start = std::chrono::high_resolution_clock::now();
    do {
        // Let's give the crash handler some time to write to the database.
        std::this_thread::sleep_for(50ms);
        std::vector<CrashReportDatabase::Report> newReports;
        std::vector<CrashReportDatabase::Report> newPendingReports;
        mCrashdatabase->GetCompletedReports(&newReports);
        mCrashdatabase->GetPendingReports(&newPendingReports);
        after = newReports.size() + newPendingReports.size();
    } while (after <= before && std::chrono::high_resolution_clock::now() - start < 5s);

    EXPECT_GT(after, before) << "The database should have recorded an additional crash!";
}

TEST_F(CrashTest, minidump_is_usable) {
    // TODO: This requires us to binplace all the .sym files ourselves.
}
