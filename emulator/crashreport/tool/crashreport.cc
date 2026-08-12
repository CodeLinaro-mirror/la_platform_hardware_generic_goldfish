// Copyright 2026 The Android Open Source Project
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
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "android/crashreport/breadcrumbs/breadcrumb_parser.h"
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

ABSL_FLAG(bool, list, false, "List local crash reports");
ABSL_FLAG(bool, l, false, "Alias for --list");
ABSL_FLAG(bool, upload, false,
          "Upload local crash report(s) to " CRASHURL_STR
          ". This attempts to upload "
          "all pending reports immediately. If the upload fails, it will be retried "
          "next time the emulator starts (only if metrics sharing is enabled). Once "
          "uploaded, the remote ID will be stored in the database, visible via --list.");
ABSL_FLAG(bool, u, false, "Alias for --upload");
ABSL_FLAG(bool, erase, false, "Erase local crash report(s)");
ABSL_FLAG(bool, e, false, "Alias for --erase");
ABSL_FLAG(std::string, minidump, "",
          "Process the given minidump file. Use 'latest' to process the most recent report in the "
          "database.");
ABSL_FLAG(std::string, format, "summary",
          "Output format for minidump analysis: 'summary' (human-readable summary), "
          "'stack' (human-readable with stack traces), or 'machine' (machine-readable). "
          "Only valid when used with --minidump.");
ABSL_FLAG(std::vector<std::string>, symbol_paths, {}, "Paths to symbol files");
ABSL_FLAG(std::string, content, "all",
          "Content to output: 'stack' (only stack trace), 'breadcrumbs' (only "
          "breadcrumbs), or 'all' (both).");
ABSL_FLAG(bool, standalone, false,
          "Process a minidump file directly without initializing or modifying the local crash "
          "database. Requires --minidump <file> and cannot be used with --list, --upload, --erase, "
          "or --minidump latest.");
ABSL_FLAG(std::string, breadcrumb_format, "text", "Breadcrumb output format: 'text' or 'mermaid'");
ABSL_FLAG(std::string, color, "auto", "Enable color output: 'always', 'never', or 'auto'");

using android::base::System;
using android::crashreport::AnnotationExtractor;
using android::crashreport::CrashReportManager;
using android::crashreport::Formatter;
using android::crashreport::MinidumpProcessor;

bool ProcessMinidump(const std::string& minidump_file, MinidumpProcessor& minidump_processor,
                     AnnotationExtractor& annotation_extractor, Formatter& formatter) {
    std::string content = absl::GetFlag(FLAGS_content);
    std::string format = absl::GetFlag(FLAGS_format);

    bool show_stack = (content == "all" || content == "stack");
    bool show_breadcrumbs = (content == "all" || content == "breadcrumbs");

    google_breakpad::ProcessState process_state;
    google_breakpad::BasicSourceLineResolver resolver;
    std::vector<std::string> symbol_paths = absl::GetFlag(FLAGS_symbol_paths);
    if (!minidump_processor.Process(minidump_file, symbol_paths, &process_state, &resolver)) {
        return false;
    }

    crashpad::FileReader reader;
    if (!reader.Open(base::FilePath(
                crashpad::ToolSupport::CommandLineArgumentToFilePathStringType(minidump_file)))) {
        LOG(ERROR) << "Minidump " << minidump_file << " could not be opened";
        return false;
    }
    nlohmann::json modules = annotation_extractor.Extract(&reader);

    if (show_stack) {
        formatter.PrintMinidumpAnalysis(process_state, &resolver, modules, format == "machine",
                                        format == "stack");
    }

    if (show_breadcrumbs) {
        if (!reader.SeekSet(0)) {
            LOG(ERROR) << "Failed to rewind minidump file for gRPC breadcrumbs";
            return false;
        }
        std::vector<uint8_t> grpc_breadcrumbs =
                annotation_extractor.ExtractAnnotationBytes(&reader, "grpc_breadcrumbs");

        if (!reader.SeekSet(0)) {
            LOG(ERROR) << "Failed to rewind minidump file for ADB breadcrumbs";
            return false;
        }
        std::vector<uint8_t> adb_breadcrumbs =
                annotation_extractor.ExtractAnnotationBytes(&reader, "adb_breadcrumbs");

        std::vector<std::vector<uint8_t>> all_breadcrumbs;
        if (!grpc_breadcrumbs.empty()) all_breadcrumbs.push_back(std::move(grpc_breadcrumbs));
        if (!adb_breadcrumbs.empty()) all_breadcrumbs.push_back(std::move(adb_breadcrumbs));

        if (!reader.SeekSet(0)) {
            LOG(ERROR) << "Failed to rewind minidump file for events breadcrumbs";
            return false;
        }
        std::vector<uint8_t> events_breadcrumbs =
                annotation_extractor.ExtractAnnotationBytes(&reader, "events_breadcrumbs");
        if (!events_breadcrumbs.empty()) all_breadcrumbs.push_back(std::move(events_breadcrumbs));

        if (!reader.SeekSet(0)) {
            LOG(ERROR) << "Failed to rewind minidump file for looper registrations";
            return false;
        }
        std::vector<uint8_t> looper_regs_bytes =
                annotation_extractor.ExtractAnnotationBytes(&reader, "looper_registrations");
        std::string looper_registrations(looper_regs_bytes.begin(), looper_regs_bytes.end());

        if (!reader.SeekSet(0)) {
            LOG(ERROR) << "Failed to rewind minidump file for partitioned event loops";
            return false;
        }
        auto looper_annotations =
                annotation_extractor.ExtractAnnotationsWithPrefix(&reader, "event_");
        for (auto& annotation : looper_annotations) {
            if (!annotation.value.empty()) {
                std::string loop_name = annotation.name.substr(6);  // Remove "event_"
                auto entries = android::crashreport::breadcrumbs::BreadcrumbParser::Parse(
                        annotation.value);
                for (const auto& entry : entries) {
                    if (entry.has_looper()) {
                        if (!looper_registrations.empty()) {
                            looper_registrations += ";";
                        }
                        absl::StrAppend(&looper_registrations, entry.looper().loop_id(), "=",
                                        loop_name);
                        break;
                    }
                }
                all_breadcrumbs.push_back(std::move(annotation.value));
            }
        }

        if (all_breadcrumbs.empty()) {
            std::cout << "No breadcrumbs found in minidump.\n";
        } else {
            uint64_t crashing_thread_id = 0;
            absl::flat_hash_map<uint64_t, uint64_t> os_tid_to_index;

            google_breakpad::Minidump dump(minidump_file);
            if (dump.Read()) {
                google_breakpad::MinidumpException* exception = dump.GetException();
                if (exception) {
                    uint32_t os_tid;
                    if (exception->GetThreadID(&os_tid)) {
                        crashing_thread_id = os_tid;
                    }
                }

                google_breakpad::MinidumpThreadList* thread_list = dump.GetThreadList();
                if (thread_list) {
                    for (uint32_t i = 0; i < thread_list->thread_count(); ++i) {
                        google_breakpad::MinidumpThread* thread = thread_list->GetThreadAtIndex(i);
                        if (thread) {
                            uint32_t os_tid;
                            if (thread->GetThreadID(&os_tid)) {
                                os_tid_to_index[os_tid] = i;
                            }
                        }
                    }
                }
                if (crashing_thread_id != 0) {
                    auto it = os_tid_to_index.find(crashing_thread_id);
                    if (it != os_tid_to_index.end()) {
                        crashing_thread_id = it->second;
                    } else {
                        crashing_thread_id = 0;
                    }
                }
            }

            using android::crashreport::breadcrumbs::BreadcrumbProcessor;
            using android::crashreport::breadcrumbs::TraceRendererFactory;

            TraceRendererFactory::RenderFormat render_format =
                    TraceRendererFactory::RenderFormat::kText;
            if (absl::GetFlag(FLAGS_breadcrumb_format) == "mermaid") {
                render_format = TraceRendererFactory::RenderFormat::kMermaid;
            }

            bool use_color = true;
            if (absl::GetFlag(FLAGS_color) == "never") {
                use_color = false;
            } else if (absl::GetFlag(FLAGS_color) == "auto") {
                use_color = isatty(fileno(stdout));
            }

            bool skip_stdout = (content == "all" && format == "machine");

            if (!skip_stdout) {
                std::string report = BreadcrumbProcessor::Process(
                        all_breadcrumbs, crashing_thread_id, render_format, use_color,
                        os_tid_to_index, &resolver, process_state.modules(), looper_registrations);
                std::cout << "\n--- Breadcrumbs ---\n" << report << "\n";
            }
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    absl::SetProgramUsageMessage(absl::StrFormat(
            "A tool to manage and analyze Android Emulator crash reports.\n\n"
            "This tool interacts with the local crash database (which stores minidumps generated\n"
            "by the emulator) and allows you to list, upload, or analyze these reports. It can\n"
            "also process minidump files directly in standalone mode.\n\n"
            "Common Workflows:\n"
            "  1. List local crash reports:\n"
            "     $ crashreport --list\n\n"
            "  2. Analyze the latest crash in the database (with symbols):\n"
            "     $ crashreport --minidump latest --symbol_paths /path/to/symbols\n\n"
            "  3. Analyze a specific minidump file directly (standalone):\n"
            "     $ crashreport --minidump /path/to/minidump.dmp --standalone --symbol_paths "
            "/path/to/symbols\n\n"
            "  4. Upload all pending crash reports to the crash server:\n"
            "     $ crashreport --upload\n"
            "     Note: This attempts to upload all pending reports immediately. If the\n"
            "           upload fails, it will be retried next time the emulator is started\n"
            "           (only if the user has opted in to metrics sharing). Once uploaded,\n"
            "           the remote report ID will be stored in the database and shown in the\n"
            "           '--list' output. This remote ID can be shared with Google in bug "
            "reports.\n\n"
            "The local crash database is located at:\n"
            "%v",
            android::crashreport::CrashSystem::databaseDirectory()));
    absl::ParseCommandLine(argc, argv);
    absl::InitializeLog();
    std::string format = absl::GetFlag(FLAGS_format);
    if (format != "summary" && format != "stack" && format != "machine") {
        LOG(ERROR) << "Invalid --format: " << format
                   << ". Must be 'summary', 'stack', or 'machine'.";
        return 1;
    }
    if (absl::GetFlag(FLAGS_minidump).empty() && format != "summary") {
        LOG(ERROR) << "--format is only valid when used with --minidump.";
        return 1;
    }
    std::string content = absl::GetFlag(FLAGS_content);
    if (content != "stack" && content != "breadcrumbs" && content != "all") {
        LOG(ERROR) << "Invalid --content: " << content
                   << ". Must be 'stack', 'breadcrumbs', or 'all'.";
        return 1;
    }
    if (absl::GetFlag(FLAGS_minidump).empty() && content != "all") {
        LOG(ERROR) << "--content is only valid when used with --minidump.";
        return 1;
    }
    std::string breadcrumb_format = absl::GetFlag(FLAGS_breadcrumb_format);
    if (breadcrumb_format != "text" && breadcrumb_format != "mermaid") {
        LOG(ERROR) << "Invalid --breadcrumb_format: " << breadcrumb_format
                   << ". Must be 'text' or 'mermaid'.";
        return 1;
    }
    if (absl::GetFlag(FLAGS_minidump).empty() && breadcrumb_format != "text") {
        LOG(ERROR) << "--breadcrumb_format is only valid when used with --minidump.";
        return 1;
    }
    if (android::base::Bazel::InBazel()) {
        if (System::GetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE").empty()) {
            System::SetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE",
                                           "/tmp/crash-report.db");
        }
        LOG(INFO) << "Running in bazel environment using crash database: "
                  << System::GetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE");
    }

    bool standalone = absl::GetFlag(FLAGS_standalone);
    bool do_list = absl::GetFlag(FLAGS_list) || absl::GetFlag(FLAGS_l);
    bool do_upload = absl::GetFlag(FLAGS_upload) || absl::GetFlag(FLAGS_u);
    bool do_erase = absl::GetFlag(FLAGS_erase) || absl::GetFlag(FLAGS_e);

    bool need_db = do_list || do_upload || do_erase || absl::GetFlag(FLAGS_minidump) == "latest";

    if (standalone && need_db) {
        LOG(ERROR) << "--standalone cannot be used with --list, --upload, --erase, or --minidump "
                      "latest";
        return 1;
    }

    if (standalone && absl::GetFlag(FLAGS_minidump).empty()) {
        LOG(ERROR) << "--standalone requires --minidump <file>";
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

    if (do_list) {
        auto reports = db_manager.GetAllReports();
        if (reports.empty()) {
            LOG(INFO) << "No reports found in database.";
        } else {
            LOG(INFO) << "Listing " << reports.size() << " reports...";
            formatter.PrintReportList(reports);
        }
    } else if (do_upload) {
        if (!db_manager.UploadCrashReports()) {
            success = false;
        }
    } else if (do_erase) {
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
    } else if (!absl::GetFlag(FLAGS_minidump).empty()) {
        std::string minidump_file = absl::GetFlag(FLAGS_minidump);
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
