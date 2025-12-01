// Copyright 2025 The Android Open Source Project
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
#include "crash_report_manager.h"

#include <chrono>
#include <sstream>
#include <thread>

#include "absl/log/log.h"

#include "android/crashreport/CrashReporter.h"
#include "client/settings.h"

namespace android {
namespace crashreport {

bool CrashReportManager::Initialize() {
    if (mDb) return true;  // Already initialized

    auto database_path = android::crashreport::CrashReporter::databaseDirectory();
    for (int i = 0; !mDb && i < 5; i++) {
        mDb = crashpad::CrashReportDatabase::Initialize(::base::FilePath(database_path));
        if (!mDb) {
            LOG(ERROR) << "Failed to initialize crash database, retrying in 1 second...";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    if (!mDb) {
        LOG(ERROR) << "Failed to initialize crash database after multiple retries.";
        return false;
    }
    mDb->GetSettings()->SetUploadsEnabled(false);
    return true;
}

std::vector<crashpad::CrashReportDatabase::Report> CrashReportManager::GetAllReports() {
    if (!mDb) {
        LOG(WARNING) << "Database not initialized!";
        return {};
    }
    std::vector<crashpad::CrashReportDatabase::Report> reports;
    std::vector<crashpad::CrashReportDatabase::Report> pendingReports;
    mDb->GetCompletedReports(&reports);
    mDb->GetPendingReports(&pendingReports);
    reports.insert(reports.end(), pendingReports.begin(), pendingReports.end());
    return reports;
}

bool CrashReportManager::DeleteReport(const crashpad::UUID& uuid) {
    if (!mDb) return false;
    return mDb->DeleteReport(uuid) == crashpad::CrashReportDatabase::kNoError;
}

bool CrashReportManager::RequestUpload(const crashpad::UUID& uuid) {
    if (!mDb) return false;
    // crashpad::CrashReportDatabase::RequestUpload always returns true
    return mDb->RequestUpload(uuid);
}

std::optional<std::string> CrashReportManager::GetLatestReportPath() {
    if (!mDb) return std::nullopt;
    auto reports = GetAllReports();
    if (reports.empty()) {
        return std::nullopt;
    }
    std::stringstream ss;
    ss << reports.back().file_path;
    // Reports are sorted by creation time, so the last one is the latest.
    return ss.str();
}

void CrashReportManager::ForEachReport(
        std::function<void(const crashpad::CrashReportDatabase::Report& report)> action) {
    if (!mDb) {
        LOG(WARNING) << "Database not initialized!";
        return;
    }
    auto reports = GetAllReports();
    for (const auto& report : reports) {
        action(report);
    }
}

}  // namespace crashreport
}  // namespace android
