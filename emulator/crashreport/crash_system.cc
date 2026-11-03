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

#include <chrono>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "android/base/system.h"
#include "android/crashreport/crash_uploader.h"
#include "android/process/process.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/crashpad_info.h"
#include "client/settings.h"
#include "goldfish/file/file.h"
#include "goldfish/tools/aemu_version.h"
#include "util/misc/uuid.h"

#ifdef _WIN32
#include <io.h>
#endif

using android::base::System;
using crashpad::CrashReportDatabase;

namespace android::crashreport {

namespace fs = std::filesystem;

#ifndef CRASHURL
#error "You must define CRASHURL to be the URL you want to upload crash reports to."
#endif
constexpr char kCrashUrl[] = CRASHURL;

class CrashSystemImpl : public CrashSystem {
  public:
    CrashSystemImpl() : client_(std::make_unique<crashpad::CrashpadClient>()) {}

    bool Active() const override { return initialized_; }

    absl::Status Initialize(const Config& config) override {
        if (initialized_) {
            return absl::FailedPreconditionError("Crash reporting is already initialized.");
        }

        if (!System::Get()->GetEnableCrashReporting()) {
            LOG(INFO) << "Crash reporting is disabled, not reporting crashes.";
            return absl::OkStatus();
        }

        if (config.handler_path.empty()) {
            return absl::NotFoundError(absl::StrCat(
                    "Crash handler not found. Pass CrashSystem::Config::handler_path or set ",
                    kCrashpadHandlerEnvVar, " to the crashpad_handler executable."));
        }
        const fs::path raw_database_dir = config.database_directory.empty()
                                                  ? DatabaseDirectory()
                                                  : config.database_directory;
        auto database_path = EnsureDatabaseDirectory(raw_database_dir);
        if (!database_path.ok()) {
            return database_path.status();
        }
        database_path_ = *std::move(database_path);
        auto metrics_path = ::base::FilePath();
        auto annotations = std::map<std::string, std::string>{
            {"prod", "AndroidEmulator"},
            {"ver", std::string(goldfish::version::GetEmulatorFullVersion())}};

        VLOG(1) << "Starting crashpad-handler: " << config.handler_path;
        auto file_path = ::base::FilePath(database_path_.native());
        std::vector<std::string> args = {"--no-rate-limit"};
#ifdef __APPLE__
        args.emplace_back(absl::StrCat("--monitor-pid=", System::Get()->GetCurrentProcessPid()));
#endif
        // Disable indirectly referenced memory capture on Windows to prevent minidump bloat.
        // This avoids scanning thread stacks for pointers and capturing arbitrary memory pages,
        // significantly reducing disk I/O and upload payload sizes.
        crashpad::CrashpadInfo* crashpad_info = crashpad::CrashpadInfo::GetCrashpadInfo();
        if (crashpad_info) {
            crashpad_info->set_gather_indirectly_referenced_memory(crashpad::TriState::kDisabled,
                                                                   0);
        }
        const bool active = client_->StartHandler(::base::FilePath(config.handler_path.native()),
                                                  file_path, metrics_path, kCrashUrl, annotations,
                                                  std::move(args), true, false);

        VLOG(1) << "Status of handler: " << (active ? "active" : "inactive");
        database_ =
                std::shared_ptr<CrashReportDatabase>(CrashReportDatabase::Initialize(file_path));
        initialized_ = active && database_;

        if (database_ && database_->GetSettings()) {
            database_->GetSettings()->SetUploadsEnabled(false);
        }

        VLOG(1) << "Using database: " << database_path_;
        // StartHandler() and CrashReportDatabase::Initialize() do not say why they failed.
        if (!active) {
            return absl::UnknownError(absl::StrCat(
                    "crashpad_handler '", config.handler_path.string(),
                    "' did not start for crash report database '", database_path_.string(),
                    "'. Verify that the executable exists, has execute permissions, and its "
                    "dependencies can be resolved."));
        }
        if (!database_) {
            return absl::UnknownError(absl::StrCat(
                    "Cannot open crash report database '", database_path_.string(),
                    "'. Check directory permissions and remove any stale database lock files."));
        }
        return absl::OkStatus();
    }

    void UploadEntries(Consent consent) override {
        if (!database_ || !initialized_) {
            LOG(INFO) << "Crash database unavailable, or not initialized, we will not report any "
                         "crashes.";
            return;
        }

        LOG(INFO) << "Storing crashdata in: " << database_path_.string() << ", detection is "
                  << (initialized_ ? "enabled" : "disabled")
                  << " for process: " << base::Process::Me()->pid();
        if (consent == Consent::ALWAYS) {
            LOG(INFO) << "Crash reports will be uploaded to: " << kCrashUrl;
        }

        std::vector<crashpad::UUID> to_remove;
        for (const auto& report : GetAllReports(*database_)) {
            if (!report.uploaded) {
                switch (consent) {
                case Consent::ALWAYS: {
                    std::shared_ptr<CrashReportDatabase> db = database_;
                    std::thread upload([db = std::move(db), report]() {
                        ProcessReport(std::move(db), report);
                    });
                    upload.detach();
                    break;
                }
                case Consent::NEVER:
                    LOG(INFO) << "No consent for crashreport " << report.uuid.ToString()
                              << ", deleting.";
                    to_remove.push_back(report.uuid);
                    break;
                case Consent::ASK:
                    LOG(INFO) << "Deferring crashreport " << report.uuid.ToString()
                              << " until upload consent is configured.";
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
    static std::vector<CrashReportDatabase::Report> GetAllReports(CrashReportDatabase& db) {
        std::vector<CrashReportDatabase::Report> reports;
        std::vector<CrashReportDatabase::Report> pending_reports;
        db.GetCompletedReports(&reports);
        db.GetPendingReports(&pending_reports);
        reports.insert(reports.end(), std::make_move_iterator(pending_reports.begin()),
                       std::make_move_iterator(pending_reports.end()));
        return reports;
    }

    static void ProcessReport(std::shared_ptr<CrashReportDatabase> db,
                              const CrashReportDatabase::Report& report) {
        using namespace std::chrono_literals;
        static constexpr std::chrono::seconds kBackoff[] = {0s,  2s,  4s,   8s,  16s,
                                                            32s, 64s, 128s, 256s};
        auto status = UploadResult::kSuccess;
        CrashReportDatabase::Report updated_report;

        for (const auto delay : kBackoff) {
            std::this_thread::sleep_for(delay);
            db->LookUpCrashReport(report.uuid, &updated_report);
            LOG(INFO) << "Attempting to send crashreport " << updated_report.uuid.ToString()
                      << " to " << kCrashUrl;
            if (!updated_report.uploaded) {
                db->RequestUpload(report.uuid);
                status = ProcessPendingReport(db.get(), report);
            } else {
                status = UploadResult::kSuccess;
            }
            if (status != UploadResult::kRetry) {
                break;
            }
        }

        if (status == UploadResult::kSuccess) {
            if (db->LookUpCrashReport(report.uuid, &updated_report) ==
                CrashReportDatabase::OperationStatus::kNoError) {
                LOG(INFO) << "Report " << updated_report.uuid.ToString()
                          << " is available remotely as: " << updated_report.id;
            }
        } else {
            LOG(WARNING) << "Failed to send report.";
        }
    }

    std::unique_ptr<crashpad::CrashpadClient> client_;
    std::shared_ptr<CrashReportDatabase> database_;
    fs::path database_path_;
    bool initialized_{false};
};

constexpr std::string_view kCrashpadDatabase = "emu-dev-crash-" VERSION ".db";
// Owner-only, because minidumps contain process memory.
constexpr unsigned kDatabaseDirectoryMode = 0700;

CrashSystem::Config CrashSystem::Config::FromEnv() {
    auto* sys = System::Get();
    return Config{
        .handler_path = sys->EnvGet(kCrashpadHandlerEnvVar),
        .database_directory = DatabaseDirectory(),
    };
}

fs::path CrashSystem::DatabaseDirectory() {
    if (auto database_directory = System::Get()->EnvGet(kCrashDatabaseEnvVar);
        !database_directory.empty()) {
        return database_directory;
    }
    return System::Get()->GetTempDir() / kCrashpadDatabase;
}

absl::StatusOr<fs::path> CrashSystem::EnsureDatabaseDirectory(const fs::path& database_path) {
    namespace file = android::base::file;
    // mkdir_recursive() does not create the first component of a relative path.
    auto absolute_path = file::make_absolute(database_path);
    if (!absolute_path.ok()) {
        return absl::Status(
                absolute_path.status().code(),
                absl::StrCat("Cannot resolve crash report database path '", database_path.string(),
                             "': ", absolute_path.status().message()));
    }
    const absl::Status created = file::mkdir_recursive(*absolute_path, kDatabaseDirectoryMode);
    // Checked even if mkdir_recursive() failed, in case another emulator created it first.
    if (file::is_dir(*absolute_path)) {
        return *std::move(absolute_path);
    }
    if (!created.ok()) {
        return absl::Status(created.code(),
                            absl::StrCat("Cannot create crash report database directory '",
                                         absolute_path->string(), "': ", created.message(),
                                         ". Set ", kCrashDatabaseEnvVar,
                                         " to a writable directory."));
    }
    return absl::FailedPreconditionError(
            absl::StrCat("Crash report database path '", absolute_path->string(),
                         "' exists but is not a directory. Remove it or set ",
                         kCrashDatabaseEnvVar, " to a directory."));
}

std::unique_ptr<CrashSystem> CrashSystem::Create() {
    return std::make_unique<CrashSystemImpl>();
}

CrashSystem& CrashSystem::Get() {
    static CrashSystemImpl s_crash_system;
    return s_crash_system;
}

}  // namespace android::crashreport
