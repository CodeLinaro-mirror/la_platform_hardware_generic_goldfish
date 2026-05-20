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
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#endif

#include <map>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "android/crashreport/breadcrumbs/breadcrumb_processor.h"
#include "android/crashreport/breadcrumbs/trace_renderer_factory.h"
#include "android/crashreport/crash_system.h"
#include "base/files/file_path.h"
#include "client/settings.h"
#include "emulator/crashreport/tool/annotation_extractor.h"
#include "emulator/crashreport/tool/crash_report_manager.h"
#include "emulator/crashreport/tool/formatter.h"
#include "emulator/crashreport/tool/minidump_processor.h"
#include "google_breakpad/processor/call_stack.h"
#include "google_breakpad/processor/minidump.h"
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
ABSL_FLAG(bool, standalone, false, "Process minidump without initializing the crash database");
ABSL_FLAG(std::string, breadcrumb_format, "text", "Breadcrumb output format: 'text' or 'mermaid'");
ABSL_FLAG(std::string, color, "auto", "Enable color output: 'always', 'never', or 'auto'");

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
        LOG(ERROR) << "Failed to process minidump " << minidump_file;
        return false;
    }

    if (!reader.SeekSet(0)) {
        LOG(ERROR) << "Failed to rewind minidump file for annotations";
        return false;
    }
    nlohmann::json modules = annotation_extractor.Extract(&reader);

    formatter.PrintMinidumpAnalysis(process_state, &resolver, modules, absl::GetFlag(FLAGS_m),
                                    absl::GetFlag(FLAGS_s));

    if (!reader.SeekSet(0)) {
        LOG(ERROR) << "Failed to rewind minidump file for breadcrumbs";
        return false;
    }
    std::vector<uint8_t> breadcrumbs =
            annotation_extractor.ExtractAnnotationBytes(&reader, "grpc_breadcrumbs");
    if (breadcrumbs.empty()) {
        std::cout << "No gRPC breadcrumbs found in minidump.\n";
    } else {
        uint64_t crashing_thread_id = 0;
        if (process_state.requesting_thread() >= 0 &&
            process_state.requesting_thread() < process_state.threads()->size()) {
            // TODO: Get the actual OS thread ID from Breakpad ProcessState if possible.
            crashing_thread_id = process_state.requesting_thread();
        }

        using android::crashreport::breadcrumbs::BreadcrumbProcessor;
        using android::crashreport::breadcrumbs::TraceRendererFactory;

        TraceRendererFactory::RenderFormat format = TraceRendererFactory::RenderFormat::kText;
        if (absl::GetFlag(FLAGS_breadcrumb_format) == "mermaid") {
            format = TraceRendererFactory::RenderFormat::kMermaid;
        }

        bool use_color = true;
        if (absl::GetFlag(FLAGS_color) == "never") {
            use_color = false;
        } else if (absl::GetFlag(FLAGS_color) == "auto") {
            use_color = isatty(fileno(stdout));
        }

        std::string report =
                BreadcrumbProcessor::Process(breadcrumbs, crashing_thread_id, format, use_color);
        std::cout << "\n--- gRPC Breadcrumbs ---\n" << report << "\n";
    }

    return true;
}

int main(int argc, char* argv[]) {
    absl::SetProgramUsageMessage(
            absl::StrFormat("List, upload and examine emulator related crashdumps.\n"
                            "The database can be found here: \n%v",
                            android::crashreport::CrashSystem::databaseDirectory()));
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

    bool standalone = absl::GetFlag(FLAGS_standalone);
    bool need_db = absl::GetFlag(FLAGS_l) || absl::GetFlag(FLAGS_u) || absl::GetFlag(FLAGS_e) ||
                   absl::GetFlag(FLAGS_d) == "latest";

    if (standalone && need_db) {
        LOG(ERROR) << "--standalone cannot be used with -l, -u, -e, or -d latest";
        return 1;
    }

    if (standalone && absl::GetFlag(FLAGS_d).empty()) {
        LOG(ERROR) << "--standalone requires -d <file>";
        return 1;
    }

    CrashReportManager db_manager;
    if (!standalone || need_db) {
        if (!db_manager.Initialize()) {
            LOG(ERROR) << "Failed to initialize CrashReportManager";
            return 1;
        }
    }
    MinidumpProcessor minidump_processor;
    AnnotationExtractor annotation_extractor;
    Formatter formatter;

    bool success = true;

    if (absl::GetFlag(FLAGS_l)) {
        auto reports = db_manager.GetAllReports();
        if (reports.empty()) {
            LOG(INFO) << "No reports found in database.";
        } else {
            LOG(INFO) << "Listing " << reports.size() << " reports...";
            formatter.PrintReportList(reports);
        }
    } else if (absl::GetFlag(FLAGS_u)) {
        auto reports = db_manager.GetAllReports();
        if (reports.empty()) {
            LOG(INFO) << "No reports found to upload.";
        } else {
            LOG(INFO) << "Checking reports for upload...";
            int requested_count = 0;
            for (const auto& report : reports) {
                if (!report.uploaded) {
                    db_manager.RequestUpload(report.uuid);
                    LOG(INFO) << "Requested upload for report " << report.uuid.ToString();
                    requested_count++;
                }
            }
            if (requested_count == 0) {
                LOG(INFO) << "All reports are already uploaded.";
            } else {
                LOG(INFO) << "Requested upload for " << requested_count << " reports.";
            }
        }
    } else if (absl::GetFlag(FLAGS_e)) {
        auto reports = db_manager.GetAllReports();
        if (reports.empty()) {
            LOG(INFO) << "No reports found to erase.";
        } else {
            LOG(INFO) << "Erasing " << reports.size() << " reports...";
            for (const auto& report : reports) {
                LOG(INFO) << "Erasing " << report.uuid.ToString();
                db_manager.DeleteReport(report.uuid);
            }
        }
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
