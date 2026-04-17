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
#include "legacy_console_controller.h"
#include "logging.h"
#include "snapshot_util.h"

namespace android::goldfish {
namespace {

using ::goldfish::async::WhenAll;
using WhenAllChardevEndpoints = std::shared_ptr<WhenAll<ChardevEndpoints>>;

using ::goldfish::modem_simulator::ModemSimulatorService;

class Launcher {
  public:
    Launcher(LauncherConfig config) : config_(std::move(config)) {
        config_.signal_handlers->SetCallback(
                [this](int signal) { forwarding_signal_handler(signal); });
        config_.event_loop
                .Post([this] {
                    if (auto s = setup_emulator_ports(config_.opts, config_.event_loop); !s.ok()) {
                        LOG(FATAL) << "Failed to set ports: " << s;
                    }

                    if (ShouldLaunchFishtank(config_.opts)) {
                        launch_fishtank();
                    }

                    auto chardevs = std::make_shared<WhenAll<ChardevEndpoints>>(
                            &config_.event_loop,
                            [this](ChardevEndpoints ce) { launch_emulator(ce); });

                    config_.event_loop.Post([this, chardevs]() { discover_netsimd(chardevs); })
                            .IgnoreError();
                    config_.event_loop.Post([this, chardevs]() { init_modem_simulator(chardevs); })
                            .IgnoreError();
                })
                .IgnoreError();
    }

    int emulator_exit_status() const { return emulator_exit_status_; }

    void join_shutdown_thread() {
        if (shutdown_thread_.joinable()) {
            shutdown_thread_.join();
        }
    }

    void forwarding_signal_handler(int signum) {
        VLOG(1) << "forwarding_signal_handler called with signum: " << signum;
        if (auto* p = emulator_process_.get()) {
            if (config_.opts.snapshot && (signum == SIGINT || signum == SIGTERM)) {
                LOG(INFO) << "Not forwarding signal " << signum
                          << " to emulator, triggering snapshot save and quit instead.";
                save_snapshot_and_quit();
                return;
            } else {
                LOG(INFO) << "Signal received, forwarding to emulator: " << signum;
                p->Kill(signum);
            }
        } else {
            // If there is no emulator process yet then we want to shutdown directly.
            shutdown();
        }
    }

    void discover_netsimd(const WhenAllChardevEndpoints& chardevs) {
        if (config_.opts.no_netsim) {
            return;
        }
        if (auto* netsimd_endpoint = config_.opts.packet_streamer_endpoint; netsimd_endpoint) {
            try_connect_netsimd(netsimd_endpoint, chardevs);
        } else {
            launch_netsimd(chardevs);
        }
    }

    void init_modem_simulator(const WhenAllChardevEndpoints& chardevs) {
        modem_simulator_service_ = ModemSimulatorService::Create(*config_.avd);
        if (modem_simulator_service_) {
            chardevs->MutableResults().modem_simulator =
                    modem_simulator_service_->ChardevEndpoint();
            chardevs->MutableResults().modem_simulator_host_id = modem_simulator_service_->HostId();
        }
    }

  private:
    static absl::StatusOr<std::shared_ptr<::goldfish::async::AsyncSocketServer>>
    open_tcp_server_port(::goldfish::async::EventLoop& event_loop,
                         ::goldfish::async::AsyncSocketFactory& factory, int port) {
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

    absl::Status hunt_for_free_port(
            ::goldfish::telnet::LegacyConsoleController& console_controller) {
        constexpr int kStartingPort = 5554;
        for (int port = kStartingPort; port < 5585; port += 2) {
            if (auto s = console_controller.Start(port); s.ok()) {
                ports_.serial_number = port;
                ports_.adb_port = port + 1;
                return absl::OkStatus();
            }
        }
        return absl::UnavailableError("No available emulator serial console port (5554-5584)");
    }

    absl::Status hunt_for_qmp_port(::goldfish::async::EventLoop& event_loop,
                                   ::goldfish::async::AsyncSocketFactory& factory) {
        constexpr int kStartingPort = 15455;
        for (int port = kStartingPort; port < kStartingPort + 100; ++port) {
            if (auto sock = open_tcp_server_port(event_loop, factory, port); sock.ok()) {
                ports_.qmp_port = port;
                qmp_port_reservation_ = *std::move(sock);
                LOG(INFO) << "QMP service will listen on port: " << port;
                return absl::OkStatus();
            }
        }
        return absl::UnavailableError("No available emulator QMP port");
    }

    absl::Status setup_emulator_ports(const AndroidOptions& opts,
                                      ::goldfish::async::EventLoop& event_loop) {
        auto console_controller = std::make_unique<::goldfish::telnet::LegacyConsoleController>(
                *config_.socket_factory, &event_loop);
        if (opts.ports) {
            // Format should be console_port,adb_port
            std::vector<std::string_view> parts = absl::StrSplit(opts.ports, ',');
            if (parts.size() != 2) {
                return absl::InvalidArgumentError(
                        absl::StrCat("Failed to parse -ports: ", opts.ports));
            }
            if (!absl::SimpleAtoi(parts[0], &ports_.serial_number)) {
                return absl::InvalidArgumentError(absl::StrCat(
                        "Failed to parse serial port number from -ports: ", opts.ports));
            }
            if (!absl::SimpleAtoi(parts[1], &ports_.adb_port)) {
                return absl::InvalidArgumentError(
                        absl::StrCat("Failed to parse ADB port number from -ports: ", opts.ports));
            }
            if (auto s = console_controller->Start(ports_.serial_number); !s.ok()) {
                LOG(FATAL) << "Unable to start the telnet console on port: " << ports_.serial_number
                           << ", reason: " << s;
            }
        } else if (opts.port) {
            // opts.port specifies the telnet console port and by default ADB port is that +1
            int port;
            if (!absl::SimpleAtoi(opts.port, &port)) {
                return absl::InvalidArgumentError(
                        absl::StrCat("Failed to parse serial port number from -port ", opts.port));
            }
            ports_.serial_number = port;
            ports_.adb_port = port + 1;
            if (auto s = console_controller->Start(port); !s.ok()) {
                LOG(FATAL) << "Unable to start the telnet console on port: " << ports_.serial_number
                           << ", reason: " << s;
            }
        } else {
            RETURN_IF_ERROR(hunt_for_free_port(*console_controller));
        }

        if (opts.snapshot && !opts.no_snapshot_save) {
            RETURN_IF_ERROR(hunt_for_qmp_port(event_loop, *config_.socket_factory));
        }

        if (ports_.adb_port < 5555 || ports_.adb_port > 5585) {
            LOG(WARNING)
                    << "ADB port specified is out of range [5555,5585], adb may not work properly: "
                    << ports_.adb_port;
        }
        if (ports_.adb_port % 2 != 1) {
            LOG(WARNING) << "ADB port specified is not an odd number, adb may not work properly: "
                         << ports_.adb_port;
        }

        console_controller_ = std::move(console_controller);
        return absl::OkStatus();
    }

    void fishtank_exit(int64_t exit_status, int term_signal) {
        LOG(INFO) << "Fishtank exited with status " << exit_status << ", signal " << term_signal;
        // TODO(whollins): Should we sigterm the emulator when the UI is closed?
        fishtank_process_.reset();
    }

    void launch_fishtank() {
        if (auto fishtank_config = ::goldfish::launcher::fishtank::launch_config(
                    config_.emulator_paths.fishtank_binary, config_.avd->Name(),
                    ports_.serial_number, config_.opts);
            fishtank_config.ok()) {
            if (auto s = config_.process_launcher->Launch(
                        *std::move(fishtank_config),
                        [this](int64_t status, int signal) { fishtank_exit(status, signal); });
                s.ok()) {
                fishtank_process_ = *std::move(s);
                LOG(INFO) << "Running fishtank as pid: " << fishtank_process_->GetPid();
            } else {
                LOG(FATAL) << "Fatal error whilst launching fishtank: " << s.status();
            }
        } else {
            LOG(FATAL) << "Fatal error whilst launching fishtank: " << fishtank_config.status();
        }
    }

    void netsimd_exit(int64_t exit_status, int term_signal) {
        LOG(INFO) << "Netsimd exited with status " << exit_status << ", signal " << term_signal;
        netsimd_process_.reset();
    }

    void launch_netsimd(const WhenAllChardevEndpoints& chardevs) {
        existing_netsimd_port_ = read_netsim_port();
        if (existing_netsimd_port_ != 0) {
            LOG(WARNING) << "netsim.ini already exists with a valid port - either previous netsimd "
                            "still running or it died without cleanup: "
                         << existing_netsimd_port_;
        }

        if (auto netsim_config =
                    netsimd_launch_config(config_.emulator_paths.netsim_binary, config_.opts);
            netsim_config.ok()) {
            if (auto s = config_.process_launcher->Launch(
                        *std::move(netsim_config),
                        [this](int64_t status, int signal) { netsimd_exit(status, signal); });
                s.ok()) {
                netsimd_process_ = *std::move(s);
                VLOG(1) << "Running netsimd as pid: " << netsimd_process_->GetPid();
                // Allow launcher to exit without waiting for netsimd process to be cleaned up.
                config_.process_launcher->ForgetProcess(*netsimd_process_);

                find_netsimd_ = config_.event_loop.ScheduleRepeating(
                        [this, chardevs] {
                            retry_countdown_ = 10;
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
        if (retry_countdown_ == 0) {
            find_netsimd_->Cancel();
            // absl::NotFoundError("Unable to determine the correct grpc endpoint for netsimd");
            LOG(FATAL) << "Unable to determine the correct grpc endpoint for netsimd";
            return;
        }
        --retry_countdown_;

        if (!netsimd_process_) {
            // netsimd itself will check whether it's already running and exit if so.
            VLOG(1) << "netsimd died, perhaps another was already running";
            if (existing_netsimd_port_ != 0) {
                find_netsimd_->Cancel();
                find_netsimd_.reset();
                LOG(WARNING) << "Connecting to already running netsimd, this likely means it was "
                                "started by another emulator instance";
                config_.event_loop
                        .Post([this, chardevs] {
                            try_connect_netsimd(absl::StrCat("localhost:", existing_netsimd_port_),
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
        if (port == existing_netsimd_port_) {
            VLOG(1) << "netsimd: Port in ini file has not yet changed: " << port;
            return;
        }

        VLOG(1) << "netsim.ini parsed successfully, grpc.port set to: " << port;
        find_netsimd_->Cancel();
        find_netsimd_.reset();
        config_.event_loop
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
            netsimd_connection_ = *std::move(connection);
            chardevs->MutableResults().netsim = netsimd_connection_->GetEndpoint().target();
        } else {
            LOG(FATAL) << "Fatal error whilst trying to connect to netsimd: "
                       << connection.status();
        }
    }

    void emulator_exit(int64_t exit_status, int term_signal) {
        LOG(INFO) << "emulator exited with status " << exit_status << ", signal " << term_signal;
        emulator_exit_status_ = exit_status;
        emulator_process_.reset();

        // Shutdown fishtank if it's running.
        if (auto* p = fishtank_process_.get()) {
            p->Kill(SIGTERM);
        }

        // Send a final ping with the crash status.
        config_.metrics_reporter->Report([exit_status,
                                          term_signal](android_studio::AndroidStudioEvent& event) {
            event.mutable_emulator_details()->set_crashes(exit_status == 0 && term_signal == 0
                                                                  ? kMetricsCrashesNone
                                                                  : kMetricsCrashesUncleanExit);
        });

        shutdown();
    }

    void launch_emulator(ChardevEndpoints chardev_endpoints) {
        LaunchQemu emulator{EmulatorConfig{ports_,
                                           chardev_endpoints,
                                           {
                                               .session_id = config_.metrics_reporter->session_id(),
                                               .writer_config = config_.metrics_writer_config,
                                           },
                                           config_.user_paths,
                                           config_.emulator_paths,
                                           *config_.avd,
                                           config_.opts}};

        // Release reservations just before launch so QEMU can bind to the ports.
        if (qmp_port_reservation_) {
            qmp_port_reservation_->Close();
            qmp_port_reservation_.reset();
        }

        if (auto emulator_config = emulator.launch_config(); emulator_config.ok()) {
            if (auto s = config_.process_launcher->Launch(
                        *std::move(emulator_config),
                        [this](int64_t status, int signal) { emulator_exit(status, signal); });
                s.ok()) {
                emulator_process_ = *std::move(s);
                LOG(INFO) << "Running emulator as pid: " << emulator_process_->GetPid();
                report_host_info_metrics();
            } else {
                LOG(FATAL) << "Fatal error whilst launching the emulator: " << s.status();
            }
        } else {
            LOG(FATAL) << "Fatal error whilst launching the emulator: " << emulator_config.status();
        }
    }

    void report_host_info_metrics() {
        config_.metrics_reporter->Report([this](android_studio::AndroidStudioEvent& event) {
            android::goldfish::FillEmulatorHostEvent(
                    event, *config_.avd,
                    /*launcher_pid=*/android::base::System::GetCurrentProcessPid(),
                    /*qemu_pid=*/emulator_process_->GetPid(), config_.opts.metrics_collection,
                    config_.opts.fuchsia);
        });
    }

    void save_snapshot_and_quit() {
        auto kill_emulator = [this]() {
            if (auto* p = emulator_process_.get()) {
                p->Kill(SIGTERM);
            } else {
                shutdown();
            }
        };

        if (!config_.opts.snapshot || config_.opts.no_snapshot_save || ports_.qmp_port == 0) {
            if (config_.opts.no_snapshot_save) {
                LOG(INFO) << "Snapshot saving disabled by -no-snapshot-save, quitting "
                             "emulator directly";
            } else {
                LOG(INFO) << "No snapshot or QMP port configured, quitting emulator directly";
            }
            kill_emulator();
            return;
        }

        SnapshotUtil::save_snapshot_and_quit(config_.event_loop, *config_.socket_factory,
                                             ports_.qmp_port, config_.opts.snapshot,
                                             config_.avd.get(), kill_emulator);
    }

    void shutdown() {
        if (serial_port_reservation_) {
            serial_port_reservation_->Close();
            serial_port_reservation_.reset();
        }

        if (qmp_port_reservation_) {
            qmp_port_reservation_->Close();
            qmp_port_reservation_.reset();
        }

        if (find_netsimd_) {
            find_netsimd_->Cancel();
            find_netsimd_.reset();
        }

        if (netsimd_connection_) {
            netsimd_connection_->Disconnect();
            netsimd_connection_.reset();
        }

        VLOG(1) << "Shutting down";
        shutdown_thread_ = std::thread([this] {
            // Shut down the signal handlers before the loop.
            config_.signal_handlers->close();
            // This can't run on the loop itself.
            if (auto s = config_.event_loop.ShutdownAndWait(std::chrono::seconds(10)); !s.ok()) {
                LOG(ERROR) << "Event loop shutdown error: " << s;
            } else {
                VLOG(1) << "Event loop shutdown succeeded";
            }
        });
    }

    const LauncherConfig config_;

    EmulatorPorts ports_;
    std::shared_ptr<::goldfish::async::AsyncSocketServer> serial_port_reservation_;
    std::shared_ptr<::goldfish::async::AsyncSocketServer> qmp_port_reservation_;
    std::unique_ptr<::goldfish::telnet::LegacyConsoleController> console_controller_;

    // Keep a handle open from the launcher to keep netsimd alive.
    // This should avoid any races between discovery and qemu device connection.
    NetsimConnection_ptr netsimd_connection_;

    std::shared_ptr<::goldfish::async::EventLoop::Timer> find_netsimd_;

    std::unique_ptr<::goldfish::async::ManagedProcess> fishtank_process_;
    std::unique_ptr<::goldfish::async::ManagedProcess> netsimd_process_;
    std::unique_ptr<::goldfish::async::ManagedProcess> emulator_process_;
    std::shared_ptr<ModemSimulatorService> modem_simulator_service_;

    int existing_netsimd_port_ = 0;
    int retry_countdown_ = 10;

    int emulator_exit_status_ = 0;

    std::thread shutdown_thread_;
};

}  // namespace

int RunLauncher(LauncherConfig config) {
    auto& event_loop = config.event_loop;

    Launcher l(std::move(config));

    if (auto s = event_loop.Run(); !s.ok()) {
        LOG(ERROR) << "Event loop run failed with error: " << s;
        return 1;
    }

    l.join_shutdown_thread();

    return l.emulator_exit_status();
}

}  // namespace android::goldfish
