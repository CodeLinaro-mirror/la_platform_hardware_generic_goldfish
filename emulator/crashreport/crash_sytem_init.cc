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

#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/absl_log.h"

#include "aemu/base/Compiler.h"
#include "android/process/process.h"
#include "android/base/system.h"
#include "android/crashreport/crash_consent.h"
#include "android/crashreport/crash_system.h"
#include "android/crashreport/uploader.h"
#include "base/files/file_path.h"
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/settings.h"
#include "crashpad/android/crashreport/crash_reporter.h"
#include "goldfish/tools/aemu_version.h"
#include "util/misc/uuid.h"

#ifdef _WIN32
#include <io.h>
#endif

using android::base::System;
using base::FilePath;
using crashpad::CrashReportDatabase;

namespace fs = std::filesystem;

namespace android::crashreport {

#ifdef NDEBUG
constexpr char CrashURL[] = "https://clients2.google.com/cr/report";
#else
constexpr char CrashURL[] = "https://clients2.google.com/cr/staging_report";
#endif

class CrashSystem {
  public:
    CrashSystem() : mClient(new crashpad::CrashpadClient()), mConsentProvider(/*TODO default?*/) {}

    // Gets a handle to single instance of crash reporter
    static CrashSystem* get();

    bool active() const { return mInitialized; }

    bool initialize() {
        if (mInitialized) {
            LOG(INFO) << "Crash reporter already initialized";
            return false;
        }
        auto handler_path = CrashReporter::handlerExe();
        if (handler_path.empty()) {
            ABSL_LOG(WARNING) << "Crash handler not found, crash reporting disabled.";
            return false;
        }
        mDatabasePath = CrashReporter::databaseDirectory();
        auto metrics_path = ::base::FilePath();
        auto annotations = std::map<std::string, std::string>{
            {"prod", "AndroidEmulator"}, {"ver", EMULATOR_FULL_VERSION_STRING}};

        ABSL_VLOG(1) << "Starting crashpad-handler: " << handler_path;
        auto file_path = ::base::FilePath(mDatabasePath.native());
        bool active = mClient->StartHandler(::base::FilePath(handler_path.native()), file_path,
                                            metrics_path, CrashURL, annotations,
                                            {"--no-rate-limit"}, true, false);

        ABSL_VLOG(1) << "Status of handler: " << (active ? "active" : "inactive");
        mDatabase = CrashReportDatabase::Initialize(file_path);
        mInitialized = active && mDatabase;

        if (mDatabase && mDatabase->GetSettings()) {
            mDatabase->GetSettings()->SetUploadsEnabled(false);
        }

        ABSL_VLOG(1) << "Using database: " << mDatabasePath;
        return mInitialized;
    }

    void uploadEntries() {
        if (!mDatabase || !mInitialized) {
            LOG(INFO) << "Crash database unavailable, or not initialized, we will not report any "
                         "crashes.";
            return;
        }

        std::string message = mDatabasePath.string();
        LOG(INFO) << "Storing crashdata in: " << message << ", detection is "
                  << (mInitialized ? "enabled" : "disabled")
                  << " for process: " << base::Process::Me()->pid();

        auto collect = mConsentProvider->consentRequired();
        bool areUploadsEnabled = (collect == CrashConsent::Consent::ALWAYS);
        if (mDatabase && areUploadsEnabled) {
            LOG(INFO) << "Crash reports will be automatically uploaded to: " << CrashURL;
            mDatabase->GetSettings()->SetUploadsEnabled(areUploadsEnabled);
        }

        // Get consent for any report that was not yet uploaded.
        std::vector<crashpad::UUID> toRemove;
        std::vector<CrashReportDatabase::Report> reports;
        std::vector<CrashReportDatabase::Report> pendingReports;
        mDatabase->GetCompletedReports(&reports);
        mDatabase->GetPendingReports(&pendingReports);
        reports.insert(reports.end(), pendingReports.begin(), pendingReports.end());

        for (const auto& report : reports) {
            if (!report.uploaded) {
                auto status = mConsentProvider->requestConsent(report);
                switch (status) {
                case CrashConsent::ReportAction::UPLOAD_REMOVE: {
                    std::thread upload([this, report]() { processReport(report); });
                    upload.detach();
                    break;
                }
                case CrashConsent::ReportAction::REMOVE:
                    LOG(INFO) << "No consent for crashreport " << report.uuid.ToString()
                              << ", deleting.";
                    toRemove.push_back(report.uuid);
                    break;
                case CrashConsent::ReportAction::UNDECIDED_KEEP:
                    LOG(INFO) << "Failed to get consent, keeping " << report.id << " for now.";
                    break;
                }
            } else {
                toRemove.push_back(report.uuid);
            }
        }

        for (const auto& report : toRemove) {
            mDatabase->DeleteReport(report);
        }
    }

    void setConsentProvider(CrashConsent* replacement) { mConsentProvider.reset(replacement); }
    void setConsentProvider(std::unique_ptr<CrashConsent> replacement) {
        mConsentProvider = std::move(replacement);
    }

  private:
    void processReport(const CrashReportDatabase::Report& report) {
        using namespace std::chrono_literals;
        std::vector<std::chrono::seconds> backoff{0s, 2s, 4s, 8s, 16s, 32s, 64s, 128s, 256s};
        auto status = UploadResult::kSuccess;
        auto wait = backoff.begin();

        mDatabase->GetSettings()->SetUploadsEnabled(true);
        mDatabase->RequestUpload(report.uuid);
        CrashReportDatabase::Report updatedReport;

        do {
            std::this_thread::sleep_for(*wait);
            mDatabase->LookUpCrashReport(report.uuid, &updatedReport);
            LOG(INFO) << "Attempting to send crashreport " << updatedReport.uuid.ToString()
                      << " to " << CrashURL;
            if (!updatedReport.uploaded) {
                status = ProcessPendingReport(mDatabase.get(), report);
            } else {
                status = UploadResult::kSuccess;
            }
            ++wait;
        } while (status == UploadResult::kRetry && wait != backoff.end());

        if (status == UploadResult::kSuccess) {
            // We should have a report id, fetch it!

            if (mDatabase->LookUpCrashReport(report.uuid, &updatedReport) ==
                CrashReportDatabase::OperationStatus::kNoError) {
                mConsentProvider->reportCompleted(updatedReport);
            }
        } else {
            LOG(WARNING) << "Failed to send report.";
        }
    }

    DISALLOW_COPY_AND_ASSIGN(CrashSystem);

    std::unique_ptr<crashpad::CrashpadClient> mClient;
    std::unique_ptr<CrashReportDatabase> mDatabase;
    std::unique_ptr<CrashConsent> mConsentProvider;
    fs::path mDatabasePath;
    bool mInitialized{false};
};

CrashSystem* CrashSystem::get() {
    static CrashSystem sCrashSystem;
    return &sCrashSystem;
}

bool inject_consent_provider(CrashConsent* myProvider) {
    auto crashSystem = CrashSystem::get();
    crashSystem->setConsentProvider(myProvider);
    return crashSystem->initialize();
}

void upload_crashes(std::unique_ptr<CrashConsent> consent) {
    const auto reporter = CrashSystem::get();
    if (reporter && reporter->active()) {
        reporter->setConsentProvider(std::move(consent));
        reporter->uploadEntries();
    }
}

}  // namespace android::crashreport

extern "C" {
using android::crashreport::CrashSystem;

bool crashhandler_init(int argc, char** argv) {
    if (!System::Get()->GetEnableCrashReporting()) {
        LOG(INFO) << "Crashreporting disabled, not reporting crashes.";
        return false;
    }

    // Catch crashes in everything.
    // This promises to not launch any threads...
    return CrashSystem::get()->initialize();
}
}
