// Copyright (C) 2024 The Android Open Source Project
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

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"

#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "android/cmdline_option.h"
#include "android/crashreport/crash_system.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/input_paths.h"
#include "android/main_help.h"
#include "android/status/status_macros.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_process_launcher.h"
#include "goldfish/async/libuv_signal_handlers.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/metrics/configure_metrics_writer.h"
#include "goldfish/metrics/metrics_reporter.h"
#include "goldfish/metrics/studio_config.h"
#include "goldfish/tools/aemu_version.h"
#include "launcher.h"
#include "logging.h"
#include "trampoline.h"
#include "uv.h"

namespace {
namespace fs = std::filesystem;

constexpr int EMULATOR_COMPATIBLE_QEMU_VERSION = 10;

constexpr int kMetricsCrashesAbandoned = 1;

// clang-format off
void ShowBanner() {
    constexpr std::string_view platform = PLATFORM " (" TARGET_CPU "), " COMPILATION_MODE;
    std::cout << absl::Substitute(
R"(                           Welcome to goldfish
       \                   The android emulator
       (o>   [ALPHA]       Version: $0-$1
       /                   Platform: $2
                           Copyright 2026 The Android Open Source Project
                           ----------------------------------------------
                           DISCLAIMER: This is an unstable alpha release.
                           Features are under active development and may
                           break, crash, or not work as expected.
)",
            VERSION, BUILD_ID, platform);
}
// clang-format on

void WarnAboutNoMetricsConsentInput() {
    printf("##############################################################################\n");
    printf("##                        WARNING - ACTION REQUIRED                         ##\n");
    printf("##  Consider using the '-metrics-collection' flag to help improve the       ##\n");
    printf("##  emulator by sending anonymized usage data. Or use the '-no-metrics'     ##\n");
    printf("##  flag to bypass this warning and turn off the metrics collection.        ##\n");
    printf("##  In a future release this warning will turn into a one-time blocking     ##\n");
    printf("##  prompt to ask for explicit user input regarding metrics collection.     ##\n");
    printf("##                                                                          ##\n");
    printf("##  Please see '-help-metrics-collection' for more details. You can use     ##\n");
    printf("##  '-metrics-to-file' or '-metrics-to-console' flags to see what type of   ##\n");
    printf("##  data is being collected by emulator as part of usage statistics.        ##\n");
    printf("##############################################################################\n");
}

std::string EmulatorMetricsUserId(const fs::path& user_directory) {
    auto path = user_directory / "userid";
    if (android::base::file::exists(path)) {
        auto id = android::base::file::read_whole_file(path, /*binary=*/false);
        if (id.ok() && !id->empty()) {
            return *id;
        } else {
            LOG(ERROR) << "failed to read emulator metrics user id, re-generating...";
        }
    }
    auto uuid = ::goldfish::metrics::Uuid::Generate().ToString();
    std::ofstream f(path);
    f << uuid;
    return uuid;
}

::goldfish::metrics::MetricsWriterConfig GetMetricsWriterConfig(const AndroidOptions& opts,
                                                                const fs::path& user_dir) {
    using enum ::goldfish::metrics::MetricsWriterType;
    if (opts.no_metrics) {
        // do nothing
        LOG(WARNING) << "Metrics disabled by user";
        return {.type = kNone};
    } else if (opts.metrics_to_console) {
        LOG(INFO) << "Metrics will be written to console";
        return {.type = kConsole};
    } else if (opts.metrics_collection) {
        LOG(INFO) << "Metrics will be uploaded directly by the emulator";
        auto user_id = ::goldfish::metrics::studio::GetMetricsUserId(user_dir);
        if (user_id.empty()) {
            // create our own one if there's no studio config
            user_id = EmulatorMetricsUserId(user_dir);
        }
        return {.type = kPlaystore,
                .playstore_url = "https://play.googleapis.com/log?format=raw",
                .user_id = user_id,
                .user_upload_consent = true};
    } else if (opts.metrics_to_file) {
        LOG(INFO) << "Metrics will be written to: " << opts.metrics_to_file;
        return {.type = kFile, .file_path = opts.metrics_to_file};
    } else {
        // No CLI overrides, use studio settings.
        switch (::goldfish::metrics::studio::GetUserMetricsOptIn(user_dir)) {
            using enum ::goldfish::metrics::studio::OptInState;
        case kOptedIn:
            LOG(INFO) << "Metrics will be written to file and uploaded by Studio";
            return {.type = kStudio,
                    .studio_spool_dir = ::goldfish::metrics::studio::GetSpoolDirectory(user_dir),
                    .user_upload_consent = true};
        case kOptedOut:
            LOG(INFO) << "Studio user opted out of metrics";
            return {.type = kNone};
        case kUnknown:
            WarnAboutNoMetricsConsentInput();
            return {.type = kNone};
        }
    }
}

void ListAvds(const AndroidOptions& opts, const android::goldfish::UserPaths& user_paths,
              bool verbose) {
    auto avds = android::goldfish::Avd::List(user_paths.avd_directory);
    for (const auto& name : avds) {
        if (verbose) {
            // Only parse the AVD when -verbose is specified.
            auto a = android::goldfish::Avd::FromName(opts, user_paths, name, /*wipe_data=*/false,
                                                      /*content_override=*/{},
                                                      /*sysdir_override=*/{});
            if (a.ok()) {
                std::cout << (*a)->Details(/*verbose=*/true) << '\n';
            } else {
                std::cout << name << " is not valid: " << a.status();
            }
        } else {
            std::cout << name << "\n";
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    if (android::base::System::GetEnvironmentVariable("ANDROID_CLI") == "1") {
        if (setsid() == -1 && errno != EPERM) {
            std::cerr << "emulator-launcher: Warning: setsid() failed: " << strerror(errno) << ".\n"
                      << "emulator-launcher: Failed to detach from parent Process Group (PGID).\n"
                      << "emulator-launcher: If running inside a CLI tool like gemini-cli, the "
                         "emulator may be terminated unexpectedly when the launcher exits.\n";
        }
    }
#endif
    absl::InitializeSymbolizer(argv[0]);

    absl::InitializeLog();

    // Take a copy of the args before the parser modifies them.
    std::vector<std::string> args_copy(argv + 1, argv + argc);

    for (int nn = 1; nn < argc; nn++) {
        const char* opt = argv[nn];
        int helpStatus = emulator_parseHelpOption(opt);
        if (helpStatus >= 0) {
            return helpStatus;
        }
    }

#ifdef __linux__
    // Bug: 417138854: work around the log spam "bad fde: FDE is really a CIE"
    std::string preload_option =
            android::base::System::Get()->GetEnvironmentVariable("ANDROID_EMU_PRELOAD_LIBGCC");
    if (preload_option == "1") {
        std::string current_preload = android::base::System::GetEnvironmentVariable("LD_PRELOAD");
        std::string libgcc_path = "/lib/x86_64-linux-gnu/libgcc_s.so.1";
        if (current_preload.empty()) {
            android::base::System::SetEnvironmentVariable("LD_PRELOAD", libgcc_path);
        } else {
            android::base::System::SetEnvironmentVariable("LD_PRELOAD",
                                                          libgcc_path + ":" + current_preload);
        }
    }
#endif
    AndroidOptions opts;
    if (android_parse_options(&argc, &argv, &opts) < 0) {
        return 1;
    }

    configureLogging(opts);

    ShowBanner();

#if defined(__linux__) || defined(__APPLE__)
    const char* kXDG_RUNTIME_DIR_NAME = "XDG_RUNTIME_DIR";
    const char* xdg_runtime_dir_val = getenv(kXDG_RUNTIME_DIR_NAME);
    if (!xdg_runtime_dir_val) {
        const char* default_runtime_dir = "/tmp";
#if defined(__APPLE__)
        const char* darwin_runtime_dir = getenv("DARWIN_USER_TEMP_DIR");
        if (darwin_runtime_dir) {
            default_runtime_dir = darwin_runtime_dir;
        } else {
            const char* darwin_temp_dir = getenv("TMPDIR");
            if (darwin_temp_dir) {
                default_runtime_dir = darwin_temp_dir;
            }
        }
#endif
        android::base::System::Get()->SetEnvironmentVariable(kXDG_RUNTIME_DIR_NAME,
                                                             default_runtime_dir);
    } else {
#if defined(__linux__)
        // Bug: 454403989
        // when systme has XDG_RUNTIME_DIR set, we need to pass it
        // to ANDROID_EMULATOR_DISCOVERY_DIR; do nothing otherwise
        android::base::System::Get()->EnvSet("ANDROID_EMULATOR_DISCOVERY_DIR", xdg_runtime_dir_val);
#endif
    }
#endif

    if (!opts.not_in_bazel && android::base::Bazel::InBazel()) {
        android::base::Bazel::StoreCommandLineArgs(argc, argv);
        // We are running in the bazel environment, make sure the plugins and binaries can be found.
        auto launcher_dir =
                fs::path(android::base::Bazel::RunfilesPath("goldfish+/emulator/launcher"));
        LOG_IF(FATAL, !android::base::file::exists(launcher_dir))
                << "Unable to locate launcher directory: " << launcher_dir;
        android::base::System::SetEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR",
                                                      launcher_dir.string());
        if (android::base::System::GetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE")
                    .empty()) {
            android::base::System::SetEnvironmentVariable(
                    "ANDROID_EMU_CRASH_REPORTING_DATABASE",
                    fs::path("/tmp/crash-report.db").string());
        }
    }

    // Check that things exist so that we can error out early if necessary.
    auto emulator_paths = android::goldfish::ResolveEmulatorPaths(opts.verbose);
    if (!emulator_paths.ok()) {
        LOG(ERROR) << "Failed to resolve emulator paths: " << emulator_paths.status();
        return 1;
    }

    if (android::goldfish::ShouldLaunchFishtank(opts) && !emulator_paths->HasFishtank()) {
        LOG(ERROR) << "Fishtank (UI) is not available in the AOSP build. "
                      "Please use the '-no-window' flag to run in headless mode.";
        return 1;
    }
    auto user_paths =
            android::goldfish::ResolveUserPaths(emulator_paths->launcher_directory, opts.verbose);
    if (!user_paths.ok()) {
        LOG(ERROR) << "Failed to resolve user paths: " << user_paths.status();
        return 1;
    }

    if (opts.list_avds) {
        ListAvds(opts, *user_paths, opts.verbose);
        return 0;
    }

    if (!android::crashreport::CrashSystem::get().initialize()) {
        LOG(WARNING) << "Failed to initialize crashreporting.";
    }

    absl::FailureSignalHandlerOptions options;
    // Call crashpad after printing stack trace.
    options.call_previous_handler = true;
    absl::InstallFailureSignalHandler(options);

    auto event_loop = goldfish::async::LibuvEventLoop::Create("LauncherLoop");

    auto reporter = std::make_unique<::goldfish::metrics::MetricsReporter>();
    auto metrics_writer_config = GetMetricsWriterConfig(opts, user_paths->user_directory);
    std::vector<std::string> crashed_metrics_sessions;
    if (metrics_writer_config.type == goldfish::metrics::MetricsWriterType::kStudio) {
        if (!::android::base::file::exists(metrics_writer_config.studio_spool_dir)) {
            if (auto s = ::android::base::file::mkdir_recursive(
                        metrics_writer_config.studio_spool_dir, 0755);
                !s.ok()) {
                LOG(ERROR)
                        << "Failed to create metrics spool directory, reporting will be disabled: "
                        << metrics_writer_config.studio_spool_dir << " - " << s;
                metrics_writer_config.type = goldfish::metrics::MetricsWriterType::kNone;
            }
        } else if (!::android::base::file::is_dir(metrics_writer_config.studio_spool_dir)) {
            LOG(ERROR) << "Metrics spool path is not a directory, reporting will be disabled: "
                       << metrics_writer_config.studio_spool_dir;
            metrics_writer_config.type = goldfish::metrics::MetricsWriterType::kNone;
        }
        crashed_metrics_sessions =
                ::goldfish::metrics::StudioFileMetricsWriter::FinalizeAbandonedSessionFiles(
                        metrics_writer_config.studio_spool_dir);
    }
    ::goldfish::metrics::ConfigureMetricsWriter(*reporter, metrics_writer_config, *event_loop);
    for (const auto& session_id : crashed_metrics_sessions) {
        LOG(WARNING) << "Reporting crashed metrics session: " << session_id;
        reporter->Report([&session_id](android_studio::AndroidStudioEvent& event) {
            event.set_studio_session_id(session_id);
            event.mutable_emulator_details()->set_crashes(
                    ::android::goldfish::kMetricsCrashesAbandoned);
        });
    }

    auto crash_consent = metrics_writer_config.user_upload_consent
                                 ? android::crashreport::Consent::ALWAYS
                                 : android::crashreport::Consent::NEVER;
    android::crashreport::CrashSystem::get().uploadEntries(crash_consent);

    // This is needed for gfxstream to be able to load GL libs.
    // TODO: consider moving this to gfxstream itself via the ANDROID_EMULATOR_LIBRARY_DIR env var.
    android::base::System::Get()->AddLibrarySearchDir(emulator_paths->library_directory.string());
    android::base::System::Get()->AddLibrarySearchDir(emulator_paths->lib64_directory.string());

    std::string avd_name;
    fs::path android_build_out;
    if (opts.avd) {
        avd_name = opts.avd;
    } else {
        // Root not used: auto android_build_root =
        // android::base::System::GetEnvironmentVariable("ANDROID_BUILD_TOP"); e.g.
        // <root>/out/target/product/emu64xa
        auto out = android::base::System::GetEnvironmentVariable("ANDROID_PRODUCT_OUT");
        if (!out.empty()) {
            avd_name = "<build>";
            android_build_out = out;
            if (!android::base::file::exists(android_build_out)) {
                LOG(ERROR) << "ANDROID_PRODUCT_OUT specified but does not exist: "
                           << android_build_out;
                return 1;
            }
            if (!android::base::file::is_dir(android_build_out)) {
                LOG(ERROR) << "ANDROID_PRODUCT_OUT is not a directory: " << android_build_out;
                return 1;
            }
        }
        // TODO also support -sysdir without -avd
    }
    if (avd_name.empty()) {
        LOG(ERROR) << "No AVD specified. Use '@foo' or '-avd foo' to launch a virtual device named "
                      "'foo'";
        return 1;
    }

    fs::path writable_content_override;
    if (opts.read_only) {
        writable_content_override = android::base::System::Get()->GetTempDir();
        VLOG(1) << "Content path overridden to: " << writable_content_override;
        android::base::file::mkdir_recursive(writable_content_override, 0755).IgnoreError();
    } else if (opts.datadir) {
        writable_content_override = fs::path(opts.datadir);
        if (!android::base::file::exists(writable_content_override)) {
            LOG(ERROR) << "-datadir specified does not exist: " << writable_content_override;
            return 1;
        }
        if (!android::base::file::is_dir(writable_content_override)) {
            LOG(ERROR) << "-datadir specified is not a directory: " << writable_content_override;
            return 1;
        }
        VLOG(1) << "Content path overridden to: " << writable_content_override;
    }

    absl::StatusOr<std::unique_ptr<android::goldfish::Avd>> avd;
    if (!android_build_out.empty()) {
        avd = android::goldfish::Avd::FromAndroidBuild(opts, *user_paths, avd_name,
                                                       android_build_out, opts.wipe_data,
                                                       writable_content_override);
    } else {
        avd = android::goldfish::Avd::FromName(opts, *user_paths, avd_name, opts.wipe_data,
                                               writable_content_override, opts.sysdir ? opts.sysdir : fs::path());
    }

    if (!avd.ok()) {
        if (avd.status().code() == absl::StatusCode::kNotFound) {
            LOG(ERROR) << "Unknown AVD name [" << avd_name
                       << "], use -list-avds to see valid list.";
            for (const auto line : absl::StrSplit(avd.status().message(), '\n')) {
                LOG(ERROR) << line;
            }
        } else {
            LOG(ERROR) << "Failed to load " << avd_name << " due to " << avd.status().message();
        }
        return 1;
    }

    if (android::goldfish::ShouldTrampolineToQemu2(**avd)) {
        android::goldfish::TrampolineToQemu2(emulator_paths->launcher_directory, std::move(args_copy));
        std::unreachable();
    }

    bool set_qemu_version = true;
    auto last_run_qemu_version = (*avd)->GetLastRunQemuVersion();
    if (!last_run_qemu_version.ok()) {
        LOG(ERROR) << "Error reading last used QEMU version for AVD " << avd_name << " due to "
                   << last_run_qemu_version.status().message();
    } else if (std::optional<int> version = last_run_qemu_version.value()) {
        if (version.value() != EMULATOR_COMPATIBLE_QEMU_VERSION) {
            LOG(ERROR) << "AVD " << avd_name
                       << " is not compatible with this emulator. Last run QEMU version: "
                       << version.value()
                       << ", compatible QEMU version: " << EMULATOR_COMPATIBLE_QEMU_VERSION
                       << ". Use -wipe-data option to reset the AVD data and use this emulator.";
            return 1;
        } else {
            VLOG(1) << "AVD last run QEMU version is compatible.";
            set_qemu_version = false;
        }
    }

    if (set_qemu_version) {
        LOG(INFO) << "Setting AVD last run compatible QEMU version to "
                  << EMULATOR_COMPATIBLE_QEMU_VERSION;
        auto s = (*avd)->SetLastRunQemuVersion(EMULATOR_COMPATIBLE_QEMU_VERSION);
        if (!s.ok()) {
            LOG(ERROR) << "Could not save last run QEMU version, error: " << s;
        }
    }

    LOG(INFO) << "Launching AVD: " << (*avd)->Details(opts.verbose);
    return android::goldfish::RunLauncher({
        .event_loop = *event_loop,
        .process_launcher = std::make_unique<::goldfish::async::UvProcessLauncher>(*event_loop),
        .signal_handlers = std::make_unique<::goldfish::async::UvSignalHandlers>(*event_loop),
        .metrics_reporter = std::move(reporter),
        .socket_factory = std::make_unique<::goldfish::async::LibuvAsyncSocketFactory>(),
        .user_paths = *std::move(user_paths),
        .emulator_paths = *std::move(emulator_paths),
        .avd = *std::move(avd),
        .opts = std::move(opts),
        .metrics_writer_config = std::move(metrics_writer_config),
    });
}
