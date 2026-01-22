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
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "android/crashreport/uploader.h"
#include "base/files/file_path.h"
#include "client/settings.h"
#include "crashpad/android/crashreport/crash_reporter.h"
#include "emulator/crashreport/tool/annotation_extractor.h"
#include "emulator/crashreport/tool/crash_report_manager.h"
#include "emulator/crashreport/tool/formatter.h"
#include "emulator/crashreport/tool/minidump_processor.h"
#include "tools/tool_support.h"

#ifdef NDEBUG
#define CRASHURL_STR "https://clients2.google.com/cr/report"
#else
#define CRASHURL_STR "https://clients2.google.com/cr/staging_report"
#endif

ABSL_FLAG(bool, l, false, "List local crash reports");
ABSL_FLAG(bool, u, false, "Upload local crash report(s) to " CRASHURL_STR);
ABSL_FLAG(bool, e, false, "Erase local crash report(s)");
ABSL_FLAG(std::string, d, "",
          "Process the given minidump file, use 'latest' for the latest minidump");
ABSL_FLAG(bool, m, false, "Output in machine-readable format (implies -d)");
ABSL_FLAG(bool, s, false, "Output stack contents (implies -d)");
ABSL_FLAG(std::vector<std::string>, symbol_paths, {}, "Paths to symbol files");

using android::base::System;
using android::crashreport::AnnotationExtractor;
using android::crashreport::CrashReportManager;
using android::crashreport::Formatter;
using android::crashreport::MinidumpProcessor;

bool ProcessMinidump(const std::string& minidump_file, MinidumpProcessor& minidump_processor,
                     AnnotationExtractor& annotation_extractor, Formatter& formatter) {
    crashpad::FileReader reader;
    if (!reader.Open(base::FilePath(
                crashpad::ToolSupport::CommandLineArgumentToFilePathStringType(minidump_file)))) {
        LOG(ERROR) << "Minidump " << minidump_file << " could not be opened";
        return false;
    }

    google_breakpad::ProcessState process_state;
    google_breakpad::BasicSourceLineResolver resolver;
    std::vector<std::string> symbol_paths = absl::GetFlag(FLAGS_symbol_paths);

    if (!minidump_processor.Process(minidump_file, symbol_paths, &process_state, &resolver)) {
        return false;
    }

    if (!reader.SeekSet(0)) {
        LOG(ERROR) << "Failed to rewind minidump file for annotations";
        return false;
    }
    nlohmann::json modules = annotation_extractor.Extract(&reader);

    formatter.PrintMinidumpAnalysis(process_state, &resolver, modules, absl::GetFlag(FLAGS_m),
                                    absl::GetFlag(FLAGS_s));
    return true;
}

int main(int argc, char* argv[]) {
    absl::SetProgramUsageMessage(
            absl::StrFormat("List, upload and examine emulator related crashdumps.\n"
                            "The database can be found here: \n%v",
                            android::crashreport::CrashReporter::databaseDirectory()));
    absl::ParseCommandLine(argc, argv);
    absl::InitializeLog();
    if (android::base::Bazel::InBazel()) {
        if (System::GetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE").empty()) {
            System::SetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE",
                                           "/tmp/crash-report.db");
        }
        LOG(INFO) << "Running in bazel environment using crash database: "
                  << System::GetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE");
    }

    CrashReportManager db_manager;
    if (!db_manager.Initialize()) {
        LOG(ERROR) << "Failed to initialize CrashReportManager";
        return 1;
    }
    MinidumpProcessor minidump_processor;
    AnnotationExtractor annotation_extractor;
    Formatter formatter;

    bool success = true;

    if (absl::GetFlag(FLAGS_l)) {
        LOG(INFO) << "Listing reports...";
        formatter.PrintReportList(db_manager.GetAllReports());
    } else if (absl::GetFlag(FLAGS_u)) {
        LOG(INFO) << "Uploading reports...";
        db_manager.ForEachReport([&](const crashpad::CrashReportDatabase::Report& report) {
            if (!report.uploaded) {
                // Re-enable uploads if needed, though it's set in Initialize
                // db_manager.mDb->GetSettings()->SetUploadsEnabled(true);
                db_manager.RequestUpload(report.uuid);
                // TODO: Uploader needs to be refactored to not need the db pointer
                // uploader.Upload(db, report);
                LOG(INFO) << "Requested upload for report " << report.uuid.ToString();
            }
        });
    } else if (absl::GetFlag(FLAGS_e)) {
        LOG(INFO) << "Erasing reports...";
        db_manager.ForEachReport([&](const crashpad::CrashReportDatabase::Report& report) {
            LOG(INFO) << "Erasing " << report.uuid.ToString();
            db_manager.DeleteReport(report.uuid);
        });
    } else if (!absl::GetFlag(FLAGS_d).empty()) {
        std::string minidump_file = absl::GetFlag(FLAGS_d);
        if (minidump_file == "latest") {
            auto latest_path = db_manager.GetLatestReportPath();
            if (!latest_path) {
                LOG(ERROR) << "No reports found to get the latest from.";
                return 1;
            }
            minidump_file = *latest_path;
        }
        if (!ProcessMinidump(minidump_file, minidump_processor, annotation_extractor, formatter)) {
            return 1;
        }
    } else {
        LOG(ERROR) << "No action specified. Use --help for usage.";
        return 1;
    }

    return success ? 0 : 1;
}
