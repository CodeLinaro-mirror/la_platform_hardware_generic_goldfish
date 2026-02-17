// Copyright 2015 The Android Open Source Project
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

#include "android/crashreport/crash_system.h"

#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"

#include "android/process/process.h"
#include "android/base/system.h"
#include "android/crashreport/crash_uploader.h"
#include "base/files/file_path.h"
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/settings.h"
#include "goldfish/tools/aemu_version.h"
#include "util/misc/uuid.h"

#ifdef _WIN32
#include <io.h>
#endif

using android::base::System;
using crashpad::CrashReportDatabase;

namespace android::crashreport {

#ifdef NDEBUG
constexpr char kCrashUrl[] = "https://clients2.google.com/cr/report";
#else
constexpr char kCrashUrl[] = "https://clients2.google.com/cr/staging_report";
#endif

class CrashSystemImpl : public CrashSystem {
  public:
    CrashSystemImpl() : client_(new crashpad::CrashpadClient()) {}

    bool active() const override { return initialized_; }

    bool initialize(Consent /*consent*/) override {
        if (initialized_) {
            LOG(INFO) << "Crash reporter already initialized";
            return false;
        }

        if (!System::Get()->GetEnableCrashReporting()) {
            LOG(INFO) << "Crashreporting disabled, not reporting crashes.";
            return false;
        }

        auto handler_path = handlerExe();
        if (handler_path.empty()) {
            LOG(WARNING) << "Crash handler not found, crash reporting disabled.";
            return false;
        }
        database_path_ = databaseDirectory();
        auto metrics_path = ::base::FilePath();
        auto annotations = std::map<std::string, std::string>{
            {"prod", "AndroidEmulator"}, {"ver", EMULATOR_FULL_VERSION_STRING}};

        VLOG(1) << "Starting crashpad-handler: " << handler_path;
        auto file_path = ::base::FilePath(database_path_.native());
        const bool active = client_->StartHandler(::base::FilePath(handler_path.native()),
                                                  file_path, metrics_path, kCrashUrl, annotations,
                                                  {"--no-rate-limit"}, true, false);

        VLOG(1) << "Status of handler: " << (active ? "active" : "inactive");
        database_ = CrashReportDatabase::Initialize(file_path);
        initialized_ = active && database_;

        if (database_ && database_->GetSettings()) {
            database_->GetSettings()->SetUploadsEnabled(false);
        }

        VLOG(1) << "Using database: " << database_path_;
        return initialized_;
    }

    void uploadEntries() override {
        if (!database_ || !initialized_) {
            LOG(INFO) << "Crash database unavailable, or not initialized, we will not report any "
                         "crashes.";
            return;
        }

        const std::string message = database_path_.string();
        LOG(INFO) << "Storing crashdata in: " << message << ", detection is "
                  << (initialized_ ? "enabled" : "disabled")
                  << " for process: " << base::Process::Me()->pid();

        const bool are_uploads_enabled = (consent_ == Consent::ALWAYS);
        if (database_ && are_uploads_enabled) {
            LOG(INFO) << "Crash reports will be automatically uploaded to: " << kCrashUrl;
            database_->GetSettings()->SetUploadsEnabled(are_uploads_enabled);
        }

        // Get consent for any report that was not yet uploaded.
        std::vector<crashpad::UUID> to_remove;
        std::vector<CrashReportDatabase::Report> reports;
        std::vector<CrashReportDatabase::Report> pending_reports;
        database_->GetCompletedReports(&reports);
        database_->GetPendingReports(&pending_reports);
        reports.insert(reports.end(), pending_reports.begin(), pending_reports.end());

        for (const auto& report : reports) {
            if (!report.uploaded) {
                auto status = consent_ == Consent::ALWAYS ? ReportAction::UPLOAD_REMOVE
                                                          : ReportAction::REMOVE;
                switch (status) {
                case ReportAction::UPLOAD_REMOVE: {
                    std::thread upload([this, report]() { ProcessReport(report); });
                    upload.detach();
                    break;
                }
                case ReportAction::REMOVE:
                    LOG(INFO) << "No consent for crashreport " << report.uuid.ToString()
                              << ", deleting.";
                    to_remove.push_back(report.uuid);
                    break;
                case ReportAction::UNDECIDED_KEEP:
                    LOG(INFO) << "Failed to get consent, keeping " << report.id << " for now.";
                    break;
                }
            } else {
                to_remove.push_back(report.uuid);
            }
        }

        for (const auto& report : to_remove) {
            database_->DeleteReport(report);
        }
    }

  private:
    void ProcessReport(const CrashReportDatabase::Report& report) {
        using namespace std::chrono_literals;
        std::vector<std::chrono::seconds> backoff{0s, 2s, 4s, 8s, 16s, 32s, 64s, 128s, 256s};
        auto status = UploadResult::kSuccess;
        auto wait = backoff.begin();

        database_->GetSettings()->SetUploadsEnabled(true);
        database_->RequestUpload(report.uuid);
        CrashReportDatabase::Report updated_report;

        do {
            std::this_thread::sleep_for(*wait);
            database_->LookUpCrashReport(report.uuid, &updated_report);
            LOG(INFO) << "Attempting to send crashreport " << updated_report.uuid.ToString()
                      << " to " << kCrashUrl;
            if (!updated_report.uploaded) {
                status = ProcessPendingReport(database_.get(), report);
            } else {
                status = UploadResult::kSuccess;
            }
            ++wait;
        } while (status == UploadResult::kRetry && wait != backoff.end());

        if (status == UploadResult::kSuccess) {
            // We should have a report id, fetch it!
            if (database_->LookUpCrashReport(report.uuid, &updated_report) ==
                CrashReportDatabase::OperationStatus::kNoError) {
                LOG(INFO) << "Report " << updated_report.uuid.ToString()
                          << " is available remotely as: " << updated_report.id;
            }
        } else {
            LOG(WARNING) << "Failed to send report.";
        }
    }

    Consent consent_ = Consent::NEVER;
    std::unique_ptr<crashpad::CrashpadClient> client_;
    std::unique_ptr<CrashReportDatabase> database_;
    fs::path database_path_;
    bool initialized_{false};
};

const constexpr std::string_view kCrashpadDatabase = "emu-dev-crash-" VERSION ".db";

fs::path CrashSystem::databaseDirectory() {
    if (auto database_directory = System::Get()->EnvGet("ANDROID_EMU_CRASH_REPORTING_DATABASE");
        !database_directory.empty()) {
        return {database_directory};
    }
    return System::Get()->GetTempDir() / kCrashpadDatabase;
}

fs::path CrashSystem::handlerExe() {
    auto from_env = System::GetEnvironmentVariable("AEMU_CRASHPAD_HANDLER");
    if (from_env.empty()) {
        LOG(ERROR) << "AEMU_CRASHPAD_HANDLER envvar is empty - unable to locate crashpad_handler";
    }

    return {from_env};
}

CrashSystem& CrashSystem::get() {
    static CrashSystemImpl s_crash_system;
    return s_crash_system;
}

}  // namespace android::crashreport
