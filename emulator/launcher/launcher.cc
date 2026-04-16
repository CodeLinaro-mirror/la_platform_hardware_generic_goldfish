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

#include "launcher.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/substitute.h"

#include "android/base/system.h"
#include "android/cmdline_option.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/input_paths.h"
#include "android/status/status_macros.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_process_launcher.h"
#include "goldfish/async/libuv_signal_handlers.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/when_all.h"
#include "goldfish/file/file.h"
#include "goldfish/metrics/metrics_reporter.h"
#include "goldfish/modem_simulator/modem_simulator_service.h"
#include "goldfish/network/endpoint.h"
#include "host_info.h"
#include "launch_fishtank.h"
#include "launch_netsimd.h"
#include "launch_qemu/emulator_config.h"
#include "launch_qemu/launch_qemu.h"
#include "logging.h"
#include "snapshot_util.h"

namespace fs = std::filesystem;

using android::base::System;
using android::goldfish::Avd;
using ::goldfish::metrics::MetricsReporter;

namespace android::goldfish {
namespace {

using ::goldfish::async::WhenAll;
using WhenAllChardevEndpoints = std::shared_ptr<WhenAll<ChardevEndpoints>>;

using ::goldfish::modem_simulator::ModemSimulatorService;

class Launcher : ::goldfish::async::UvProcessLauncher {
  public:
    Launcher(::goldfish::async::LibuvEventLoop& event_loop, ResolvedInputPaths resolved_paths,
             std::unique_ptr<Avd> avd, AndroidOptions opts,
             std::unique_ptr<MetricsReporter> reporter,
             ::goldfish::metrics::MetricsWriterConfig metrics_writer_config)
            : UvProcessLauncher(static_cast<uv_loop_t*>(event_loop.GetRawLoop()))
            , mEventLoop(event_loop)
            , mResolvedPaths(std::move(resolved_paths))
            , mAvd(std::move(avd))
            , mOpts(std::move(opts))
            , mReporter(std::move(reporter))
            , mMetricsConfig{.session_id = mReporter->session_id(),
                             .writer_config = std::move(metrics_writer_config)}
            , mSignalHandlers(event_loop,
                              [this](int signal) { forwarding_signal_handler(signal); }) {
        mEventLoop
                .Post([this] {
                    if (auto s = setup_emulator_ports(mOpts, mEventLoop); !s.ok()) {
                        LOG(FATAL) << "Failed to set ports: " << s;
                    }

                    if (ShouldLaunchFishtank(mOpts)) {
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
            event.mutable_emulator_details()->set_crashes(exit_status == 0 && term_signal == 0
                                                                  ? kMetricsCrashesNone
                                                                  : kMetricsCrashesUncleanExit);
        });

        l.shutdown();
    }

    void launch_emulator(ChardevEndpoints chardev_endpoints) {
        LaunchQemu emulator{EmulatorConfig{mPorts, chardev_endpoints, mMetricsConfig,
                                           mResolvedPaths, *mAvd, mOpts}};

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
            android::goldfish::FillEmulatorHostEvent(
                    event, *mAvd, /*launcher_pid=*/android::base::System::GetCurrentProcessPid(),
                    /*qemu_pid=*/GetPid(mEmulatorProcess), mOpts.metrics_collection, mOpts.fuchsia);
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

}  // namespace

int RunLauncher(::goldfish::async::LibuvEventLoop& event_loop, ResolvedInputPaths resolved_paths,
                std::unique_ptr<Avd> avd, AndroidOptions opts,
                std::unique_ptr<::goldfish::metrics::MetricsReporter> reporter,
                ::goldfish::metrics::MetricsWriterConfig metrics_writer_config) {
    Launcher l(event_loop, std::move(resolved_paths), std::move(avd), opts, std::move(reporter),
               std::move(metrics_writer_config));

    if (auto s = event_loop.Run(); !s.ok()) {
        LOG(ERROR) << "Event loop run failed with error: " << s;
        return 1;
    }

    l.join_shutdown_thread();

    return l.emulator_exit_status();
}

}  // namespace android::goldfish
