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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

#include "aemu/base/utils/status_macros.h"
#include "android/base/bazel_info.h"
#include "android/base/file/file.h"
#include "android/base/system.h"
#include "android/cmdline_option.h"
#include "android/crashreport/crash_consent.h"
#include "android/crashreport/crash_initializer.h"
#include "android/crashreport/crash_system.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/emulator_config.h"
#include "android/goldfish/input_paths.h"
#include "android/main_help.h"
#include "emulator.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_process_launcher.h"
#include "goldfish/async/libuv_signal_handlers.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/when_all.h"
#include "goldfish/modem_simulator/modem_simulator_service.h"
#include "goldfish/network/endpoint.h"
#include "goldfish/tools/aemu_version.h"
#include "logging.h"
#include "netsimd.h"

namespace fs = std::filesystem;

using android::base::Bazel;
using android::base::System;
using android::goldfish::Avd;
using android::goldfish::Emulator;

namespace android::goldfish {
namespace {

using ::goldfish::async::WhenAll;
using WhenAllChardevEndpoints = std::shared_ptr<WhenAll<ChardevEndpoints>>;

using ::goldfish::modem_simulator::ModemSimulatorService;

static void show_banner() {
    constexpr std::string_view platform = PLATFORM " (" TARGET_CPU "), " COMPILATION_MODE;
    std::cout << "              .: .          \n";
    std::cout << "            .    -            Welcome to goldfish\n";
    std::cout << "        ==:    .-+       =-   The android emulator\n";
    std::cout << "     :+            :  #:  .   Version: " VERSION << "-" << BUILD_ID << "\n";
    std::cout << "    %     @         :@-   -   Platform: " << platform << "\n";
    std::cout << "   :              *=   - -    Copyright 2024 The Android Open Source Project\n";
    std::cout << "   :  -<      :+++      - \n";
    std::cout << "    = _ _.*= .            \n";
}

class Launcher : public ::goldfish::async::UvProcessLauncher {
  public:
    Launcher(::goldfish::async::LibuvEventLoop& event_loop, ResolvedInputPaths resolved_paths,
             std::unique_ptr<Avd> avd, AndroidOptions opts)
            : UvProcessLauncher(static_cast<uv_loop_t*>(event_loop.GetRawLoop()))
            , mEventLoop(event_loop)
            , mResolvedPaths(std::move(resolved_paths))
            , mAvd(std::move(avd))
            , mOpts(std::move(opts))
            , mSignalHandlers(event_loop,
                              [this](int signal) { forwarding_signal_handler(signal); }) {
        mEventLoop.Post([this] {
            if (auto s = setup_emulator_ports(mOpts, mEventLoop); !s.ok()) {
                LOG(FATAL) << "Failed to set ports: " << s;
            }

            auto chardevs = std::make_shared<WhenAll<ChardevEndpoints>>(
                    &mEventLoop, [this](ChardevEndpoints ce) { launch_emulator(ce); });

            mEventLoop.Post([this, chardevs]() { discover_netsimd(chardevs); }).IgnoreError();
            mEventLoop.Post([this, chardevs]() { init_modem_simulator(chardevs); }).IgnoreError();
        }).IgnoreError();
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
        LOG(INFO) << "Signal received, forwarding to emulator: " << signum;
        if (auto* p = mEmulatorProcess.get()) {
            uv_process_kill(p, signum);
        } else {
            // If there is no emulator process yet then we want to shutdown directly.
            shutdown();
        }
    }

    static void netsimd_exit(uv_process_t* req, int64_t exit_status, int term_signal) {
        LOG(INFO) << "Netsimd exited with status " << exit_status << ", signal " << term_signal;
        Launcher& l = static_cast<Launcher&>(GetLauncher(*req));
        CloseHandle(std::move(l.mNetsimdProcess));
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
            chardevs->MutableResults().netsim = mNetsimdConnection->getEndpoint().target();
        } else {
            LOG(FATAL) << "Fatal error whilst trying to connect to netsimd: "
                       << connection.status();
        }
    }

    static void emulator_exit(uv_process_t* req, int64_t exit_status, int term_signal) {
        LOG(INFO) << "emulator exited with status " << exit_status << ", signal " << term_signal;
        Launcher& l = static_cast<Launcher&>(GetLauncher(*req));
        l.mEmulatorExitStatus = exit_status;
        CloseHandle(std::move(l.mEmulatorProcess));

        l.shutdown();
    }

    void launch_emulator(ChardevEndpoints chardev_endpoints) {
        Emulator emulator{mPorts, std::move(chardev_endpoints), std::move(mResolvedPaths),
                          std::move(mAvd), std::move(mOpts)};

        if (auto emulator_config = emulator.launch_config(); emulator_config.ok()) {
            if (auto s = Launch(*std::move(emulator_config), &emulator_exit); s.ok()) {
                mEmulatorProcess = *std::move(s);
                LOG(INFO) << "Running emulator as pid: " << GetPid(mEmulatorProcess);
            } else {
                LOG(FATAL) << "Fatal error whilst launching the emulator: " << s.status();
            }
        } else {
            LOG(FATAL) << "Fatal error whilst launching the emulator: " << emulator_config.status();
        }
    }

    void shutdown() {
        if (mSerialPortReservation) {
            mSerialPortReservation->Close();
            mSerialPortReservation.reset();
        }

        if (mFindNetsimd) {
            mFindNetsimd->Cancel();
            mFindNetsimd.reset();
        }

        if (mNetsimdConnection) {
            mNetsimdConnection->disconnect();
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

    ::goldfish::async::UvSignalHandlers mSignalHandlers;

    EmulatorPorts mPorts;
    std::shared_ptr<::goldfish::async::AsyncSocketServer> mSerialPortReservation;

    // Keep a handle open from the launcher to keep netsimd alive.
    // This should avoid any races between discovery and qemu device connection.
    NetsimConnection_ptr mNetsimdConnection;

    std::shared_ptr<::goldfish::async::EventLoop::Timer> mFindNetsimd;

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
        auto a = Avd::FromName(resolved_paths, name, sysdir_override ? sysdir_override : "");
        if (!a.status().ok()) {
            std::cout << name << "is not valid: " << a.status().message();
        } else {
            std::cout << (*a)->Details(verbose) << '\n';
        }
    }
}

class CrashConsentProviderAlways : public android::crashreport::CrashConsent {
  public:
    ~CrashConsentProviderAlways() override = default;
    Consent consentRequired() override { return Consent::ALWAYS; }
    ReportAction requestConsent(const crashpad::CrashReportDatabase::Report& report) override {
        return ReportAction::UPLOAD_REMOVE;
    }
};

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
    auto resolved_paths = android::goldfish::ResolvePaths(opts.verbose);
    if (!resolved_paths.ok()) {
        LOG(ERROR) << "Failed to resolve paths: " << resolved_paths.status();
        return 1;
    }

    if (opts.list_avds) {
        android::goldfish::list_avds(*resolved_paths, opts.verbose, opts.sysdir);
        return 0;
    }

    if (!crashhandler_init(argc, argv)) {
        LOG(WARNING) << "Failed to initialize crashreporting.";
    }

    // TODO change consent before release
    android::crashreport::upload_crashes(
            std::make_unique<android::goldfish::CrashConsentProviderAlways>());

    absl::FailureSignalHandlerOptions options;
    // Call crashpad after printing stack trace.
    options.call_previous_handler = true;
    absl::InstallFailureSignalHandler(options);

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

    auto avd = Avd::FromName(*resolved_paths, name, sysdir_override, writable_content_override);
    if (!avd.ok()) {
        LOG(ERROR) << "Failed to load " << name << " due to " << avd.status().message();
        return 1;
    }

    auto event_loop = goldfish::async::LibuvEventLoop::Create();

    android::goldfish::Launcher l(*event_loop, *std::move(resolved_paths), *std::move(avd), opts);

    if (auto s = event_loop->Run(); !s.ok()) {
        LOG(ERROR) << "Event loop run failed with error: " << s;
        return 1;
    }

    l.join_shutdown_thread();

    return l.emulator_exit_status();
}
