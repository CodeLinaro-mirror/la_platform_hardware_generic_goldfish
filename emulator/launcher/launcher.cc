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

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/substitute.h"

#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "android/cmdline_option.h"
#include "android/crashreport/crash_system.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/emulator_config.h"
#include "android/goldfish/input_paths.h"
#include "android/main_help.h"
#include "android/status/status_macros.h"
#include "emulator.h"
#include "fishtank.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_process_launcher.h"
#include "goldfish/async/libuv_signal_handlers.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/when_all.h"
#include "goldfish/file/file.h"
#include "goldfish/metrics/configure_metrics_writer.h"
#include "goldfish/metrics/metrics_reporter.h"
#include "goldfish/metrics/studio_config.h"
#include "goldfish/modem_simulator/modem_simulator_service.h"
#include "goldfish/network/endpoint.h"
#include "goldfish/tools/aemu_version.h"
#include "host_info.h"
#include "logging.h"
#include "netsimd.h"
#include "snapshot_util.h"

namespace fs = std::filesystem;

using android::base::Bazel;
using android::base::System;
using android::goldfish::Avd;
using android::goldfish::Emulator;
using ::goldfish::metrics::MetricsReporter;

namespace android::goldfish {
namespace {

using ::goldfish::async::WhenAll;
using WhenAllChardevEndpoints = std::shared_ptr<WhenAll<ChardevEndpoints>>;

using ::goldfish::modem_simulator::ModemSimulatorService;

constexpr int EMULATOR_COMPATIBLE_QEMU_VERSION = 10;

constexpr int kMetricsCrashesNone = 0;
constexpr int kMetricsCrashesAbandoned = 1;
constexpr int kMetricsCrashesUncleanExit = 2;

// clang-format off
void show_banner() {
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

bool should_launch_fishtank(const AndroidOptions& opts) {
    return !opts.no_window;
}

void warnAboutNoMetricsConsentInput() {
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

std::string EmulatorMetricsUserId(const fs::path &user_directory) {
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

::goldfish::metrics::MetricsWriterConfig get_metrics_writer_config(const AndroidOptions& opts, const ResolvedInputPaths &resolved_paths) {
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
        auto user_id = ::goldfish::metrics::studio::GetMetricsUserId(resolved_paths.user_directory);
        if (user_id.empty()) {
            // create our own one if there's no studio config
            user_id = EmulatorMetricsUserId(resolved_paths.user_directory);
        }
        // TODO(476380758): Switch from staging to prod clearcut after verification.
        return {.type = kPlaystore, .playstore_url="https://play.googleapis.com/staging/log?format=raw", .user_id = user_id, .user_upload_consent = true};
    } else if (opts.metrics_to_file) {
        LOG(INFO) << "Metrics will be written to: " << opts.metrics_to_file;
        return {.type = kFile, .file_path = opts.metrics_to_file};
    } else {
        // No CLI overrides, use studio settings.
        switch (::goldfish::metrics::studio::GetUserMetricsOptIn(resolved_paths.user_directory)) {
            using enum ::goldfish::metrics::studio::OptInState;
        case kOptedIn:
            LOG(INFO) << "Metrics will be written to file and uploaded by Studio";
            return {.type = kStudio, .studio_spool_dir = ::goldfish::metrics::studio::GetSpoolDirectory(resolved_paths.user_directory), .user_upload_consent = true};
        case kOptedOut:
            LOG(INFO) << "Studio user opted out of metrics";
            return {.type = kNone};
        case kUnknown:
            warnAboutNoMetricsConsentInput();
            return {.type = kNone};
        }
    }
}

class Launcher : ::goldfish::async::UvProcessLauncher {
  public:
    Launcher(::goldfish::async::LibuvEventLoop& event_loop, ResolvedInputPaths resolved_paths,
             std::unique_ptr<Avd> avd, AndroidOptions opts, std::unique_ptr<MetricsReporter> reporter, ::goldfish::metrics::MetricsWriterConfig metrics_writer_config)
            : UvProcessLauncher(static_cast<uv_loop_t*>(event_loop.GetRawLoop()))
            , mEventLoop(event_loop)
            , mResolvedPaths(std::move(resolved_paths))
            , mAvd(std::move(avd))
            , mOpts(std::move(opts))
            , mReporter(std::move(reporter))
            , mMetricsConfig{.session_id = mReporter->session_id(), .writer_config = std::move(metrics_writer_config)}
            , mSignalHandlers(event_loop,
                              [this](int signal) { forwarding_signal_handler(signal); }) {
        mEventLoop
                .Post([this] {
                    if (auto s = setup_emulator_ports(mOpts, mEventLoop); !s.ok()) {
                        LOG(FATAL) << "Failed to set ports: " << s;
                    }

                    if (should_launch_fishtank(mOpts)) {
                        launch_fishtank();
                    }

                    auto chardevs = std::make_shared<WhenAll<ChardevEndpoints>>(
                            &mEventLoop, [this](ChardevEndpoints ce) { launch_emulator(ce); });

                    mEventLoop.Post([this, chardevs]() { discover_netsimd(chardevs); })
                            .IgnoreError();
                    mEventLoop.Post([this, chardevs]() { init_modem_simulator(chardevs); })
                            .IgnoreError();
                })
                .IgnoreError();
    }

    int emulator_exit_status() const { return mEmulatorExitStatus; }

    void join_shutdown_thread() {
        if (mShutdownThread.joinable()) {
            mShutdownThread.join();
        }
    }

    void discover_netsimd(const WhenAllChardevEndpoints& chardevs) {
        if (mOpts.no_netsim) {
            chardevs->MutableResults().netsim = "";
        } else if (auto netsimd_endpoint = mOpts.packet_streamer_endpoint; netsimd_endpoint) {
            try_connect_netsimd(netsimd_endpoint, chardevs);
        } else {
            launch_netsimd(chardevs);
        }
    }

    void init_modem_simulator(const WhenAllChardevEndpoints& chardevs) {
        modem_simulator_service_ = ModemSimulatorService::Create(*mAvd);
        if (modem_simulator_service_) {
            chardevs->MutableResults().modem_simulator =
                    modem_simulator_service_->ChardevEndpoint();
            chardevs->MutableResults().modem_simulator_host_id = modem_simulator_service_->HostId();
        }
    }

  private:
    absl::StatusOr<std::shared_ptr<::goldfish::async::AsyncSocketServer>> open_tcp_server_port(
            ::goldfish::async::EventLoop& event_loop,
            ::goldfish::async::LibuvAsyncSocketFactory& factory, int port) {
        using ::goldfish::network::ToEndpoint;
        using ::goldfish::network::ToIpv4Address;
        const auto k_ipv4_loopback = ToIpv4Address(127, 0, 0, 1);

        auto e = ToEndpoint(k_ipv4_loopback, port);
        auto sock = factory.CreateServer(&event_loop, std::move(e), [](auto) {
            VLOG(1) << "Ignoring connection to serial port reservation server";
            return false;
        });
        if (sock != nullptr) {
            return sock;
        }
        return absl::UnavailableError("unable to open server socket");
    }

    absl::Status hunt_for_free_port(::goldfish::async::EventLoop& event_loop,
                                    ::goldfish::async::LibuvAsyncSocketFactory& factory) {
        constexpr int kStartingPort = 5554;
        std::shared_ptr<::goldfish::async::AsyncSocketServer> sock;
        for (int port = kStartingPort; port < 5585; port += 2) {
            if (auto sock = open_tcp_server_port(event_loop, factory, port); sock.ok()) {
                mPorts.serial_number = port;
                mPorts.adb_port = port + 1;
                mSerialPortReservation = *std::move(sock);
                return absl::OkStatus();
            }
        }

        return absl::UnavailableError("No available emulator serial console port (5554-5584)");
    }

    absl::Status hunt_for_qmp_port(::goldfish::async::EventLoop& event_loop,
                                   ::goldfish::async::LibuvAsyncSocketFactory& factory) {
        constexpr int kStartingPort = 15455;
        for (int port = kStartingPort; port < kStartingPort + 100; ++port) {
            if (auto sock = open_tcp_server_port(event_loop, factory, port); sock.ok()) {
                mPorts.qmp_port = port;
                mQmpPortReservation = *std::move(sock);
                LOG(INFO) << "QMP service will listen on port: " << port;
                return absl::OkStatus();
            }
        }
        return absl::UnavailableError("No available emulator QMP port");
    }

    absl::Status setup_emulator_ports(const AndroidOptions& opts,
                                      ::goldfish::async::EventLoop& event_loop) {
        auto factory = std::make_unique<::goldfish::async::LibuvAsyncSocketFactory>();
        if (opts.ports) {
            // Format should be console_port,adb_port
            std::vector<std::string_view> parts = absl::StrSplit(opts.ports, ',');
            if (parts.size() != 2) {
                return absl::InvalidArgumentError(
                        absl::StrCat("Failed to parse -ports: ", opts.ports));
            }
            if (!absl::SimpleAtoi(parts[0], &mPorts.serial_number)) {
                return absl::InvalidArgumentError(absl::StrCat(
                        "Failed to parse serial port number from -ports: ", opts.ports));
            }
            if (!absl::SimpleAtoi(parts[1], &mPorts.adb_port)) {
                return absl::InvalidArgumentError(
                        absl::StrCat("Failed to parse ADB port number from -ports: ", opts.ports));
            }
            ASSIGN_OR_RETURN(mSerialPortReservation,
                             open_tcp_server_port(event_loop, *factory, mPorts.serial_number));
        } else if (opts.port) {
            // opts.port specifies the telnet console port and by default ADB port is that +1
            int port;
            if (!absl::SimpleAtoi(opts.port, &port)) {
                return absl::InvalidArgumentError(
                        absl::StrCat("Failed to parse serial port number from -port ", opts.port));
            }
            mPorts.serial_number = port;
            mPorts.adb_port = port + 1;
            ASSIGN_OR_RETURN(mSerialPortReservation,
                             open_tcp_server_port(event_loop, *factory, port));
        } else {
            RETURN_IF_ERROR(hunt_for_free_port(event_loop, *factory));
        }

        if (opts.snapshot && !opts.no_snapshot_save) {
            RETURN_IF_ERROR(hunt_for_qmp_port(event_loop, *factory));
        }

        if (mPorts.adb_port < 5555 || mPorts.adb_port > 5585) {
            LOG(WARNING)
                    << "ADB port specified is out of range [5555,5585], adb may not work properly: "
                    << mPorts.adb_port;
        }
        if (mPorts.adb_port % 2 != 1) {
            LOG(WARNING) << "ADB port specified is not an odd number, adb may not work properly: "
                         << mPorts.adb_port;
        }

        return absl::OkStatus();
    }

    void forwarding_signal_handler(int signum) {
        if (auto* p = mEmulatorProcess.get()) {
            if (mOpts.snapshot && (signum == SIGINT || signum == SIGTERM)) {
                LOG(INFO) << "Not forwarding signal " << signum
                          << " to emulator, triggering snapshot save and quit instead.";
                save_snapshot_and_quit();
                return;
            } else {
                LOG(INFO) << "Signal received, forwarding to emulator: " << signum;
                uv_process_kill(p, signum);
            }
        } else {
            // If there is no emulator process yet then we want to shutdown directly.
            shutdown();
        }
    }

    static void fishtank_exit(uv_process_t* req, int64_t exit_status, int term_signal) {
        LOG(INFO) << "Fishtank exited with status " << exit_status << ", signal " << term_signal;
        Launcher& l = static_cast<Launcher&>(GetLauncher(*req));
        // TODO(whollins): Should we sigterm the emulator when the UI is closed?
        l.mFishtankProcess.reset();
    }

    void launch_fishtank() {
        if (auto fishtank_config = ::goldfish::launcher::fishtank::launch_config(
                    mResolvedPaths.fishtank_binary, mAvd->Name(), mPorts.serial_number, mOpts);
            fishtank_config.ok()) {
            if (auto s = Launch(*std::move(fishtank_config), &fishtank_exit); s.ok()) {
                mFishtankProcess = *std::move(s);
                LOG(INFO) << "Running fishtank as pid: " << GetPid(mFishtankProcess);
            } else {
                LOG(FATAL) << "Fatal error whilst launching fishtank: " << s.status();
            }
        } else {
            LOG(FATAL) << "Fatal error whilst launching fishtank: " << fishtank_config.status();
        }
    }

    static void netsimd_exit(uv_process_t* req, int64_t exit_status, int term_signal) {
        LOG(INFO) << "Netsimd exited with status " << exit_status << ", signal " << term_signal;
        Launcher& l = static_cast<Launcher&>(GetLauncher(*req));
        l.mNetsimdProcess.reset();
    }

    void launch_netsimd(const WhenAllChardevEndpoints& chardevs) {
        mExistingNetsimdPort = read_netsim_port();
        if (mExistingNetsimdPort != 0) {
            LOG(WARNING) << "netsim.ini already exists with a valid port - either previous netsimd "
                            "still running or it died without cleanup: "
                         << mExistingNetsimdPort;
        }

        if (auto netsim_config = netsimd_launch_config(mResolvedPaths.netsim_binary, mOpts);
            netsim_config.ok()) {
            if (auto s = Launch(*std::move(netsim_config), &netsimd_exit); s.ok()) {
                mNetsimdProcess = *std::move(s);
                VLOG(1) << "Running netsimd as pid: " << GetPid(mNetsimdProcess);
                // Allow launcher to exit without waiting for netsimd process to be cleaned up.
                ForgetUvProcess(mNetsimdProcess);

                mFindNetsimd = mEventLoop.ScheduleRepeating(
                        [this, chardevs] {
                            mRetryCountDown = 10;
                            find_netsimd_endpoint(chardevs);
                        },
                        std::chrono::seconds(1), std::chrono::seconds(1));
            } else {
                LOG(FATAL) << "Fatal error whilst launching netsimd: " << s.status();
            }
        } else {
            LOG(FATAL) << "Fatal error whilst launching netsimd: " << netsim_config.status();
        }
    }

    void find_netsimd_endpoint(const WhenAllChardevEndpoints& chardevs) {
        if (mRetryCountDown == 0) {
            mFindNetsimd->Cancel();
            // absl::NotFoundError("Unable to determine the correct grpc endpoint for netsimd");
            LOG(FATAL) << "Unable to determine the correct grpc endpoint for netsimd";
            return;
        }
        --mRetryCountDown;

        if (!mNetsimdProcess) {
            // netsimd itself will check whether it's already running and exit if so.
            VLOG(1) << "netsimd died, perhaps another was already running";
            if (mExistingNetsimdPort != 0) {
                mFindNetsimd->Cancel();
                mFindNetsimd.reset();
                LOG(WARNING) << "Connecting to already running netsimd, this likely means it was "
                                "started by another emulator instance";
                mEventLoop
                        .Post([this, chardevs] {
                            try_connect_netsimd(absl::StrCat("localhost:", mExistingNetsimdPort),
                                                chardevs);
                        })
                        .IgnoreError();
                return;
            } else {
                LOG(FATAL) << "netsimd died and there was no existing port to connect to";
            }
        }

        int port = read_netsim_port();
        if (port == 0) {
            VLOG(1) << "netsimd: Port not yet available";
            return;
        }
        // We expect the port to change, if it doesn't then something strange has happened.
        if (port == mExistingNetsimdPort) {
            VLOG(1) << "netsimd: Port in ini file has not yet changed: " << port;
            return;
        }

        VLOG(1) << "netsim.ini parsed successfully, grpc.port set to: " << port;
        mFindNetsimd->Cancel();
        mFindNetsimd.reset();
        mEventLoop
                .Post([this, port, chardevs] {
                    try_connect_netsimd(absl::StrCat("localhost:", port), chardevs);
                })
                .IgnoreError();
    }

    void try_connect_netsimd(const std::string& netsimd_endpoint,
                             const WhenAllChardevEndpoints& chardevs) {
        constexpr absl::Duration kConnectionDeadline = absl::Seconds(5);

        // Blocking
        VLOG(1) << "Trying to connect to netsimd at: " << netsimd_endpoint;
        if (auto connection = connect_to_netsim(netsimd_endpoint, kConnectionDeadline);
            connection.ok()) {
            VLOG(1) << "Launcher connection to netsim established";
            mNetsimdConnection = *std::move(connection);
            chardevs->MutableResults().netsim = mNetsimdConnection->GetEndpoint().target();
        } else {
            LOG(FATAL) << "Fatal error whilst trying to connect to netsimd: "
                       << connection.status();
        }
    }

    static void emulator_exit(uv_process_t* req, int64_t exit_status, int term_signal) {
        LOG(INFO) << "emulator exited with status " << exit_status << ", signal " << term_signal;
        Launcher& l = static_cast<Launcher&>(GetLauncher(*req));
        l.mEmulatorExitStatus = exit_status;
        l.mEmulatorProcess.reset();

        // Shutdown fishtank if it's running.
        if (auto* p = l.mFishtankProcess.get()) {
            uv_process_kill(p, SIGTERM);
        }

        // Send a final ping with the crash status.
        l.mReporter->Report([exit_status, term_signal](android_studio::AndroidStudioEvent& event) {
            event.mutable_emulator_details()->set_crashes(exit_status == 0 && term_signal == 0 ? kMetricsCrashesNone : kMetricsCrashesUncleanExit);
        });

        l.shutdown();
    }

    void launch_emulator(ChardevEndpoints chardev_endpoints) {
        Emulator emulator{mPorts, chardev_endpoints, mMetricsConfig, mResolvedPaths, *mAvd, mOpts};

        // Release reservations just before launch so QEMU can bind to the ports.
        if (mQmpPortReservation) {
            mQmpPortReservation->Close();
            mQmpPortReservation.reset();
        }

        if (auto emulator_config = emulator.launch_config(); emulator_config.ok()) {
            if (auto s = Launch(*std::move(emulator_config), &emulator_exit); s.ok()) {
                mEmulatorProcess = *std::move(s);
                LOG(INFO) << "Running emulator as pid: " << GetPid(mEmulatorProcess);
                report_host_info_metrics();
            } else {
                LOG(FATAL) << "Fatal error whilst launching the emulator: " << s.status();
            }
        } else {
            LOG(FATAL) << "Fatal error whilst launching the emulator: " << emulator_config.status();
        }
    }

    void report_host_info_metrics() {
        mReporter->Report([this](android_studio::AndroidStudioEvent& event) {
            android::goldfish::FillEmulatorHostEvent(event, *mAvd, /*launcher_pid=*/android::base::System::GetCurrentProcessPid(), /*qemu_pid=*/GetPid(mEmulatorProcess), mOpts.metrics_collection, mOpts.fuchsia);
        });
    }

    void save_snapshot_and_quit() {
        auto kill_emulator = [this]() {
            if (auto* p = mEmulatorProcess.get()) {
                uv_process_kill(p, SIGTERM);
            } else {
                shutdown();
            }
        };

        if (!mOpts.snapshot || mOpts.no_snapshot_save || mPorts.qmp_port == 0) {
            if (mOpts.no_snapshot_save) {
                LOG(INFO) << "Snapshot saving disabled by -no-snapshot-save, quitting "
                             "emulator directly";
            } else {
                LOG(INFO) << "No snapshot or QMP port configured, quitting emulator directly";
            }
            kill_emulator();
            return;
        }

        SnapshotUtil::save_snapshot_and_quit(mEventLoop, mPorts.qmp_port, mOpts.snapshot,
                                             mAvd.get(), kill_emulator);
    }

    void shutdown() {
        if (mSerialPortReservation) {
            mSerialPortReservation->Close();
            mSerialPortReservation.reset();
        }

        if (mQmpPortReservation) {
            mQmpPortReservation->Close();
            mQmpPortReservation.reset();
        }

        if (mFindNetsimd) {
            mFindNetsimd->Cancel();
            mFindNetsimd.reset();
        }

        if (mNetsimdConnection) {
            mNetsimdConnection->Disconnect();
            mNetsimdConnection.reset();
        }

        VLOG(1) << "Shutting down";
        mShutdownThread = std::thread([this] {
            // Shut down the signal handlers before the loop.
            mSignalHandlers.close();
            // This can't run on the loop itself.
            if (auto s = mEventLoop.ShutdownAndWait(std::chrono::seconds(10)); !s.ok()) {
                LOG(ERROR) << "Event loop shutdown error: " << s;
            } else {
                VLOG(1) << "Event loop shutdown succeeded";
            }
        });
    }

    ::goldfish::async::EventLoop& mEventLoop;

    ResolvedInputPaths mResolvedPaths;
    std::unique_ptr<Avd> mAvd;
    AndroidOptions mOpts;

    std::unique_ptr<MetricsReporter> mReporter;
    MetricsConfig mMetricsConfig;

    ::goldfish::async::UvSignalHandlers mSignalHandlers;

    EmulatorPorts mPorts;
    std::shared_ptr<::goldfish::async::AsyncSocketServer> mSerialPortReservation;
    std::shared_ptr<::goldfish::async::AsyncSocketServer> mQmpPortReservation;

    // Keep a handle open from the launcher to keep netsimd alive.
    // This should avoid any races between discovery and qemu device connection.
    NetsimConnection_ptr mNetsimdConnection;

    std::shared_ptr<::goldfish::async::EventLoop::Timer> mFindNetsimd;

    ProcessHandle mFishtankProcess;
    ProcessHandle mNetsimdProcess;
    ProcessHandle mEmulatorProcess;
    std::shared_ptr<ModemSimulatorService> modem_simulator_service_;

    int mExistingNetsimdPort = 0;
    int mRetryCountDown = 10;

    int mEmulatorExitStatus = 0;

    std::thread mShutdownThread;
};

void list_avds(const ResolvedInputPaths& resolved_paths, bool verbose, char* sysdir_override) {
    auto avds = Avd::List(resolved_paths.avd_directory);
    for (const auto& name : avds) {
        auto a = Avd::FromName(resolved_paths, name, false, sysdir_override ? sysdir_override : "");
        if (!a.status().ok()) {
            std::cout << name << "is not valid: " << a.status().message();
        } else {
            std::cout << (*a)->Details(verbose) << '\n';
        }
    }
}

}  // namespace
}  // namespace android::goldfish

int main(int argc, char** argv) {
    absl::InitializeSymbolizer(argv[0]);

    // libuv recommends calling this from the parent before spawning any children.
    uv_disable_stdio_inheritance();

    absl::InitializeLog();

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
            System::Get()->GetEnvironmentVariable("ANDROID_EMU_PRELOAD_LIBGCC");
    if (preload_option == "1") {
        std::string current_preload = System::GetEnvironmentVariable("LD_PRELOAD");
        std::string libgcc_path = "/lib/x86_64-linux-gnu/libgcc_s.so.1";
        if (current_preload.empty()) {
            System::SetEnvironmentVariable("LD_PRELOAD", libgcc_path);
        } else {
            System::SetEnvironmentVariable("LD_PRELOAD", libgcc_path + ":" + current_preload);
        }
    }
#endif
    AndroidOptions opts;
    if (android_parse_options(&argc, &argv, &opts) < 0) {
        return 1;
    }

    configureLogging(opts);

    Bazel::StoreCommandLineArgs(argc, argv);
    android::goldfish::show_banner();

    // we will use bazel to run emulator in normal mode,
    // this -not-in-bazel option is used to force inBazel
    // to return false;
    if (opts.not_in_bazel) {
        Bazel::SetNotInBazel();
    }

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
        System::Get()->SetEnvironmentVariable(kXDG_RUNTIME_DIR_NAME, default_runtime_dir);
    } else {
#if defined(__linux__)
        // Bug: 454403989
        // when systme has XDG_RUNTIME_DIR set, we need to pass it
        // to ANDROID_EMULATOR_DISCOVERY_DIR; do nothing otherwise
        System::Get()->EnvSet("ANDROID_EMULATOR_DISCOVERY_DIR", xdg_runtime_dir_val);
#endif
    }
#endif

    if (Bazel::InBazel()) {
        // We are running in the bazel environment, make sure the plugins and binaries can be found.
        auto launcher_dir = fs::path(Bazel::RunfilesPath("goldfish+/emulator/launcher"));
        LOG_IF(FATAL, !android::base::file::exists(launcher_dir))
                << "Unable to locate launcher directory: " << launcher_dir;
        System::SetEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR", launcher_dir.string());
        if (System::GetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE").empty()) {
            System::SetEnvironmentVariable("ANDROID_EMU_CRASH_REPORTING_DATABASE",
                                           fs::path("/tmp/crash-report.db").string());
        }
    }

    // Check that things exist so that we can error out early if necessary.
    auto resolved_paths = android::goldfish::ResolvePaths(opts.verbose, android::goldfish::should_launch_fishtank(opts));
    if (!resolved_paths.ok()) {
        LOG(ERROR) << "Failed to resolve paths: " << resolved_paths.status();
        return 1;
    }

    if (opts.list_avds) {
        android::goldfish::list_avds(*resolved_paths, opts.verbose, opts.sysdir);
        return 0;
    }

    if (!android::crashreport::CrashSystem::get().initialize()) {
        LOG(WARNING) << "Failed to initialize crashreporting.";
    }

    absl::FailureSignalHandlerOptions options;
    // Call crashpad after printing stack trace.
    options.call_previous_handler = true;
    absl::InstallFailureSignalHandler(options);

    auto event_loop = goldfish::async::LibuvEventLoop::Create();

    auto reporter = std::make_unique<MetricsReporter>();
    auto metrics_writer_config = android::goldfish::get_metrics_writer_config(opts, *resolved_paths);
    std::vector<std::string> crashed_metrics_sessions;
    if (metrics_writer_config.type == goldfish::metrics::MetricsWriterType::kStudio) {
        if (!::android::base::file::exists(metrics_writer_config.studio_spool_dir)) {
            if (auto s = ::android::base::file::mkdir_recursive(metrics_writer_config.studio_spool_dir, 0755); !s.ok()) {
                LOG(ERROR) << "Failed to create metrics spool directory, reporting will be disabled: " << metrics_writer_config.studio_spool_dir << " - " << s;
                metrics_writer_config.type = goldfish::metrics::MetricsWriterType::kNone;
            }
        } else if (!::android::base::file::is_dir(metrics_writer_config.studio_spool_dir)) {
            LOG(ERROR) << "Metrics spool path is not a directory, reporting will be disabled: " << metrics_writer_config.studio_spool_dir;
            metrics_writer_config.type = goldfish::metrics::MetricsWriterType::kNone;
        }
        crashed_metrics_sessions = ::goldfish::metrics::StudioFileMetricsWriter::FinalizeAbandonedSessionFiles(metrics_writer_config.studio_spool_dir);
    }
    ::goldfish::metrics::ConfigureMetricsWriter(*reporter, metrics_writer_config, *event_loop);
    for (const auto &session_id : crashed_metrics_sessions) {
        LOG(WARNING) << "Reporting crashed metrics session: " << session_id;
        reporter->Report([&session_id] (android_studio::AndroidStudioEvent& event) {
            event.set_studio_session_id(session_id);
            event.mutable_emulator_details()->set_crashes(::android::goldfish::kMetricsCrashesAbandoned);
        });
    }

    auto crash_consent = metrics_writer_config.user_upload_consent ? android::crashreport::Consent::ALWAYS : android::crashreport::Consent::NEVER;
    android::crashreport::CrashSystem::get().uploadEntries(crash_consent);

    // This is needed for gfxstream to be able to load GL libs.
    // TODO: consider moving this to gfxstream itself via the ANDROID_EMULATOR_LIBRARY_DIR env var.
    System::Get()->AddLibrarySearchDir(resolved_paths->library_directory.string());
    System::Get()->AddLibrarySearchDir(resolved_paths->lib64_directory.string());

    if (!opts.avd) {
        LOG(ERROR) << "No AVD specified. Use '@foo' or '-avd foo' to launch a virtual device named "
                      "'foo'";
        return 1;
    }

    auto name = opts.avd;

    fs::path sysdir_override;
    if (opts.sysdir) {
        sysdir_override = fs::path(opts.sysdir);
    }

    fs::path writable_content_override;
    if (opts.read_only) {
        writable_content_override = System::Get()->GetTempDir();
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

    auto avd = Avd::FromName(*resolved_paths, name, opts.wipe_data, sysdir_override, writable_content_override);
    if (!avd.ok()) {
        LOG(ERROR) << "Failed to load " << name << " due to " << avd.status().message();
        return 1;
    }

    bool set_qemu_version = true;
    auto last_run_qemu_version = (*avd)->GetLastRunQemuVersion();
    if (!last_run_qemu_version.ok()) {
        LOG(ERROR) << "Error reading last used QEMU version for AVD " << name << " due to "
                   << last_run_qemu_version.status().message();
    } else if (std::optional<int> version = last_run_qemu_version.value()) {
        if (version.value() != android::goldfish::EMULATOR_COMPATIBLE_QEMU_VERSION) {
            LOG(ERROR) << "AVD " << name
                       << "is not compatible with this emulator. Last run QEMU version: "
                       << version.value()
                       << ", compatible QEMU version: " << android::goldfish::EMULATOR_COMPATIBLE_QEMU_VERSION
                       << ". Use -wipe-data option to reset the AVD data and use this emulator.";
            return 1;
        } else {
            VLOG(1) << "AVD last run QEMU version is compatible.";
            set_qemu_version = false;
        }
    }

    if (set_qemu_version) {
        LOG(INFO) << "Setting AVD last run compatible QEMU version to "
                  << android::goldfish::EMULATOR_COMPATIBLE_QEMU_VERSION;
        auto s = (*avd)->SetLastRunQemuVersion(android::goldfish::EMULATOR_COMPATIBLE_QEMU_VERSION);
        if (!s.ok()) {
            LOG(ERROR) << "Could not save last run QEMU version, error: " << s;
        }
    }

    android::goldfish::Launcher l(*event_loop, *std::move(resolved_paths), *std::move(avd), opts, std::move(reporter), std::move(metrics_writer_config));

    if (auto s = event_loop->Run(); !s.ok()) {
        LOG(ERROR) << "Event loop run failed with error: " << s;
        return 1;
    }

    l.join_shutdown_thread();

    return l.emulator_exit_status();
}
