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

#include <fstream>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "android/goldfish/avd.h"
#include "android/goldfish/input_paths.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/process_launcher.h"
#include "goldfish/async/signal_handlers.h"
#include "goldfish/async/testing/fake_async_socket.h"
#include "goldfish/metrics/metrics_reporter.h"
#include "goldfish/metrics/metrics_writer.h"
#include "goldfish/network/endpoint.h"
#include "mock_avd.h"
#include "uv.h"

namespace android::goldfish {
namespace {

using testing::_;
using testing::Return;

MATCHER_P(IsPort, port, "") {
    return ExplainMatchResult(port, ::goldfish::network::GetPortFromEndpoint(arg), result_listener);
}

class MockAsyncSocketServer : public ::goldfish::async::AsyncSocketServer {
  public:
    MOCK_METHOD(void, Close, (), (override));
    MOCK_METHOD(::goldfish::network::Endpoint, GetEndpoint, (), (const, override));
    MOCK_METHOD(::goldfish::async::EventLoop*, GetLoop, (), (const, override));
};

class MockAsyncSocketFactory : public ::goldfish::async::AsyncSocketFactory {
  public:
    MOCK_METHOD(std::shared_ptr<::goldfish::async::AsyncSocket>, CreateSocket,
                (::goldfish::async::EventLoop * loop,
                 const ::goldfish::network::Endpoint& endpoint),
                (override));
    MOCK_METHOD(std::shared_ptr<::goldfish::async::AsyncSocketServer>, CreateServer,
                (::goldfish::async::EventLoop * loop, const ::goldfish::network::Endpoint& endpoint,
                 ::goldfish::async::AsyncSocketServer::ConnectCallback connect_callback,
                 ::goldfish::async::AsyncSocketServer::LoopProvider loop_provider),
                (override));
};

class MockManagedProcess : public ::goldfish::async::ManagedProcess {
  public:
    MOCK_METHOD(int, GetPid, (), (const, override));
    MOCK_METHOD(void, Kill, (int signum), (override));
};

class MockProcessLauncher : public ::goldfish::async::ProcessLauncher {
  public:
    MOCK_METHOD(absl::StatusOr<std::unique_ptr<::goldfish::async::ManagedProcess>>, Launch,
                (const ::goldfish::async::LaunchConfig& config, ExitCallback exit_cb), (override));
    MOCK_METHOD(void, ForgetProcess, (const ::goldfish::async::ManagedProcess& process),
                (override));
};

class MockSignalHandlers : public ::goldfish::async::SignalHandlers {
  public:
    MOCK_METHOD(void, SetCallback, (Callback signal_cb), (override));
    MOCK_METHOD(void, close, (), (override));
};

class MockMetricsWriter : public ::goldfish::metrics::MetricsWriter {
  public:
    MOCK_METHOD(void, Write, (::goldfish::metrics::MetricsEvent event), (override));
};

class LauncherTest : public testing::Test {
  protected:
    void SetUp() override {
        temp_dir = fs::path(testing::TempDir()) / "launcher_test";
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir / "bin");
        fs::create_directories(temp_dir / "lib");
        fs::create_directories(temp_dir / "lib64");
        fs::create_directories(temp_dir / "share" / "qemu");

        // Setup dummy qemu-img
#ifdef _WIN32
        fs::path qemu_img = temp_dir / "bin" / "qemu-img.cmd";
        std::ofstream qemu_img_file(qemu_img);
        qemu_img_file << "@echo off\n"
                      << "for %%a in (%*) do (\n"
                      << "  set \"arg=%%~a\"\n"
                      << "  if \"%%~xa\"==\".qcow2\" (\n"
                      << "    powershell -Command \"[IO.File]::WriteAllBytes('%%~a', "
                         "[byte[]](0x51, 0x46, 0x49, 0xFB))\"\n"
                      << "  )\n"
                      << ")\n"
                      << "exit /b 0\n";
#else
        fs::path qemu_img = temp_dir / "bin" / "qemu-img";
        std::ofstream qemu_img_file(qemu_img);
        qemu_img_file << "#!/bin/sh\n"
                      << "for arg in \"$@\"; do\n"
                      << "  case \"$arg\" in\n"
                      << "    *.qcow2) printf \"QFI\\373\" > \"$arg\" ;;\n"
                      << "  esac\n"
                      << "done\n"
                      << "exit 0\n";
#endif
        qemu_img_file.close();
        fs::permissions(qemu_img, fs::perms::owner_exec | fs::perms::owner_all);

        emulator_paths.launcher_binary = temp_dir / "emulator";
        emulator_paths.launcher_directory = temp_dir;
        emulator_paths.binary_directory = temp_dir / "bin";
        emulator_paths.library_directory = temp_dir / "lib";
        emulator_paths.lib64_directory = temp_dir / "lib64";
        emulator_paths.bios_directory = temp_dir / "share" / "qemu";
        emulator_paths.qemu_img_binary = qemu_img;
        // No need to have .exe for Windows on these binaries as they aren't executed.
        emulator_paths.qemu_system_x86_binary = temp_dir / "bin" / "qemu-system-x86_64";
        emulator_paths.qemu_system_arm_binary = temp_dir / "bin" / "qemu-system-aarch64";
        emulator_paths.netsim_binary = temp_dir / "bin" / "netsimd";
        emulator_paths.crashpad_handler_binary = temp_dir / "bin" / "crashpad_handler";
        emulator_paths.fishtank_binary = temp_dir / "fishtank" / "fishtank";

        user_paths.user_directory = temp_dir / "user";
        user_paths.avd_directory = temp_dir / "avd";
        user_paths.sdk_directory = temp_dir / "sdk";
        user_paths.discovery_directory = temp_dir / "discovery";

        std::memset(&opts, 0, sizeof(opts));
        opts.no_window = true;
        opts.no_netsim = true;
        opts.avd = "test_avd";
#ifdef _WIN32
        // Windows VMs don't support nested virt.
        opts.no_accel = true;
#endif

        owned_launcher = std::make_unique<MockProcessLauncher>();
        mock_launcher = owned_launcher.get();
        owned_signal_handlers = std::make_unique<MockSignalHandlers>();
        mock_signal_handlers = owned_signal_handlers.get();
        owned_socket_factory = std::make_unique<MockAsyncSocketFactory>();
        mock_socket_factory = owned_socket_factory.get();

        EXPECT_CALL(*mock_socket_factory, CreateServer(_, _, _, _))
                .WillRepeatedly([](::goldfish::async::EventLoop* loop,
                                   const ::goldfish::network::Endpoint& endpoint,
                                   ::goldfish::async::AsyncSocketServer::ConnectCallback,
                                   ::goldfish::async::AsyncSocketServer::LoopProvider) {
                    auto server = std::make_shared<MockAsyncSocketServer>();
                    ON_CALL(*server, GetEndpoint()).WillByDefault(Return(endpoint));
                    return server;
                });

        event_loop = ::goldfish::async::LibuvEventLoop::Create("MainLoop");

        ON_CALL(*mock_launcher, Launch(_, _))
                .WillByDefault([&](const ::goldfish::async::LaunchConfig&,
                                   ::goldfish::async::ProcessLauncher::ExitCallback exit_cb) {
                    auto process = std::make_unique<MockManagedProcess>();
                    ON_CALL(*process, GetPid()).WillByDefault(Return(1234));
                    return absl::StatusOr<std::unique_ptr<::goldfish::async::ManagedProcess>>(
                            std::move(process));
                });
    }

    void TearDown() override { fs::remove_all(temp_dir); }

    int RunLauncher(std::unique_ptr<MockAvd> avd,
                    std::unique_ptr<::goldfish::metrics::MetricsReporter> reporter = nullptr) {
        if (!reporter) {
            reporter = std::make_unique<::goldfish::metrics::MetricsReporter>();
        }
        int status = android::goldfish::RunLauncher({
            .event_loop = *event_loop,
            .process_launcher = std::move(owned_launcher),
            .signal_handlers = std::move(owned_signal_handlers),
            .metrics_reporter = std::move(reporter),
            .socket_factory = std::move(owned_socket_factory),
            .user_paths = std::move(user_paths),
            .emulator_paths = std::move(emulator_paths),
            .avd = std::move(avd),
            .opts = std::move(opts),
        });

        return status;
    }

    std::thread TriggerEmulatorScript(absl::Notification& launched, std::function<void()> script,
                                      absl::Notification* wait_for_extra = nullptr) {
        return std::thread([&, script = std::move(script), wait_for_extra]() {
            launched.WaitForNotification();
            if (wait_for_extra) {
                wait_for_extra->WaitForNotification();
            }
            // Small sleep to ensure launcher has actually finished setup.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            script();
        });
    }

    std::thread TriggerEmulatorExit(absl::Notification& launched,
                                    ::goldfish::async::ProcessLauncher::ExitCallback& exit_cb,
                                    int exit_code = 0,
                                    absl::Notification* wait_for_extra = nullptr) {
        return TriggerEmulatorScript(
                launched,
                [this, &exit_cb, exit_code]() {
                    event_loop
                            ->Post([exit_cb, exit_code]() {
                                if (exit_cb) exit_cb(exit_code, 0);
                            })
                            .IgnoreError();
                },
                wait_for_extra);
    }

    std::unique_ptr<MockAvd> CreateMockAvd() {
        auto avd = std::make_unique<testing::NiceMock<MockAvd>>();
        fs::path avd_dir = temp_dir / "avd_content";
        fs::create_directories(avd_dir);
        EXPECT_CALL(*avd, Name()).WillRepeatedly(Return("test_avd"));
        EXPECT_CALL(*avd, GetContentPath()).WillRepeatedly(Return(avd_dir));
        EXPECT_CALL(*avd, Details(_)).WillRepeatedly(Return("test_details"));
#if defined(__arm64__)
        EXPECT_CALL(*avd, Arch()).WillRepeatedly(Return(Avd::CpuArchitecture::kArm));
        EXPECT_CALL(*avd, Abi()).WillRepeatedly(Return("arm64-v8a"));
#else
        EXPECT_CALL(*avd, Arch()).WillRepeatedly(Return(Avd::CpuArchitecture::kX86));
        EXPECT_CALL(*avd, Abi()).WillRepeatedly(Return("x86_64"));
#endif
        EXPECT_CALL(*avd, GetDeviceType()).WillRepeatedly(Return(DeviceType::kPhone));
        EXPECT_CALL(*avd, GetLastRunQemuVersion()).WillRepeatedly(Return(std::optional<int>(10)));
        EXPECT_CALL(*avd, Hw()).WillRepeatedly(testing::ReturnRef(hw));
        EXPECT_CALL(*avd, ApiLevel()).WillRepeatedly(testing::Return(30));

        system_image_paths.system_image = temp_dir / "system.img";
        system_image_paths.vendor_image = temp_dir / "vendor.img";
        system_image_paths.ramdisk_image = temp_dir / "ramdisk.img";
        system_image_paths.data_dir = temp_dir / "data";
        system_image_paths.kernel_image = temp_dir / "kernel-ranchu";
        system_image_paths.kernel_cmdline = temp_dir / "kernel_cmdline.txt";
        system_image_paths.build_properties = temp_dir / "build.prop";
        system_image_paths.advanced_features = temp_dir / "advancedFeatures.ini";
        system_image_paths.verified_boot_params = temp_dir / "VerifiedBootParams.textproto";
        system_image_paths.encryption_key_image = temp_dir / "encryptionkey.img";

        // Create dummy images
        std::ofstream(system_image_paths.build_properties).close();
        std::ofstream(system_image_paths.advanced_features).close();
        std::ofstream(system_image_paths.verified_boot_params).close();
        std::ofstream(system_image_paths.kernel_cmdline).close();
        std::ofstream(system_image_paths.kernel_image).close();
        std::ofstream(system_image_paths.ramdisk_image).close();
        std::ofstream(system_image_paths.encryption_key_image).close();
        std::ofstream(system_image_paths.system_image).close();
        std::ofstream(system_image_paths.vendor_image).close();
        fs::create_directories(system_image_paths.data_dir);
        std::ofstream(system_image_paths.data_dir / "empty_data_disk").close();

        EXPECT_CALL(*avd, GetSystemImagePaths())
                .WillRepeatedly(testing::ReturnRef(system_image_paths));
        return avd;
    }

    fs::path ExpectedQemuBinary() {
#if defined(__arm64__)
        return emulator_paths.qemu_system_arm_binary;
#else
        return emulator_paths.qemu_system_x86_binary;
#endif
    }

    void ExpectEmulatorLaunch(
            absl::Notification& launched,
            ::goldfish::async::ProcessLauncher::ExitCallback& exit_cb_out,
            MockManagedProcess** process_out = nullptr,
            std::optional<testing::Matcher<const ::goldfish::async::LaunchConfig&>> config_matcher =
                    std::nullopt) {
        auto matcher = config_matcher.has_value()
                               ? *config_matcher
                               : testing::Field(&::goldfish::async::LaunchConfig::exe_path,
                                                ExpectedQemuBinary());
        EXPECT_CALL(*mock_launcher, Launch(matcher, _))
                .WillOnce([&, process_out](
                                  const ::goldfish::async::LaunchConfig&,
                                  ::goldfish::async::ProcessLauncher::ExitCallback exit_cb) {
                    exit_cb_out = std::move(exit_cb);
                    launched.Notify();
                    auto process = std::make_unique<MockManagedProcess>();
                    EXPECT_CALL(*process, GetPid()).WillRepeatedly(Return(1234));
                    if (process_out) {
                        *process_out = process.get();
                    }
                    return absl::StatusOr<std::unique_ptr<::goldfish::async::ManagedProcess>>(
                            std::move(process));
                });
    }

    fs::path temp_dir;
    UserPaths user_paths;
    EmulatorPaths emulator_paths;
    SystemImagePaths system_image_paths;
    AndroidOptions opts;
    HardwareConfig hw;
    std::unique_ptr<::goldfish::async::LibuvEventLoop> event_loop;
    std::unique_ptr<MockProcessLauncher> owned_launcher;
    MockProcessLauncher* mock_launcher;
    std::unique_ptr<MockSignalHandlers> owned_signal_handlers;
    MockSignalHandlers* mock_signal_handlers;
    std::unique_ptr<MockAsyncSocketFactory> owned_socket_factory;
    MockAsyncSocketFactory* mock_socket_factory;
};

TEST_F(LauncherTest, CanLaunchEmulator) {
    auto avd = CreateMockAvd();

    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;
    absl::Notification launched;

    ExpectEmulatorLaunch(launched, emulator_exit_cb);

    EXPECT_CALL(*mock_signal_handlers, SetCallback(_)).Times(1);

    std::thread trigger = TriggerEmulatorExit(launched, emulator_exit_cb);

    auto reporter = std::make_unique<::goldfish::metrics::MetricsReporter>();
    auto mock_writer = std::make_unique<MockMetricsWriter>();
    EXPECT_CALL(*mock_writer, Write(_)).Times(testing::AtLeast(1));
    reporter->SetWriter(std::move(mock_writer));

    int status = RunLauncher(std::move(avd), std::move(reporter));

    trigger.join();
    EXPECT_EQ(status, 0);
}

TEST_F(LauncherTest, ForwardsSignalToEmulator) {
    auto avd = CreateMockAvd();

    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;
    ::goldfish::async::SignalHandlers::Callback launcher_signal_cb;
    absl::Notification launched;
    absl::Notification callback_set;

    // We need to capture the process object to verify Kill()
    MockManagedProcess* mock_process_ptr = nullptr;
    ExpectEmulatorLaunch(launched, emulator_exit_cb, &mock_process_ptr);

    EXPECT_CALL(*mock_signal_handlers, SetCallback(_))
            .WillOnce([&](::goldfish::async::SignalHandlers::Callback signal_cb) {
                launcher_signal_cb = std::move(signal_cb);
                callback_set.Notify();
            });

    std::thread trigger = TriggerEmulatorScript(
            launched,
            [&]() {
                // Verify Kill is called on the process
                EXPECT_CALL(*mock_process_ptr, Kill(SIGINT)).Times(1);
                event_loop->Post([&]() { launcher_signal_cb(SIGINT); }).IgnoreError();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                event_loop->Post([&]() { emulator_exit_cb(0, 0); }).IgnoreError();
            },
            &callback_set);

    auto reporter = std::make_unique<::goldfish::metrics::MetricsReporter>();
    auto mock_writer = std::make_unique<MockMetricsWriter>();
    EXPECT_CALL(*mock_writer, Write(_)).Times(testing::AtLeast(1));
    reporter->SetWriter(std::move(mock_writer));

    RunLauncher(std::move(avd), std::move(reporter));

    trigger.join();
}

TEST_F(LauncherTest, LaunchesFishtankWhenWindowIsEnabled) {
    auto avd = CreateMockAvd();
    opts.no_window = false;

    absl::Notification fishtank_launched;
    ::goldfish::async::ProcessLauncher::ExitCallback fishtank_exit_cb;
    absl::Notification emulator_launched;
    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;

    EXPECT_CALL(*mock_launcher, Launch(testing::Field(&::goldfish::async::LaunchConfig::exe_path,
                                                      emulator_paths.fishtank_binary),
                                       _))
            .WillOnce([&](const ::goldfish::async::LaunchConfig&,
                          ::goldfish::async::ProcessLauncher::ExitCallback exit_cb) {
                fishtank_exit_cb = std::move(exit_cb);
                fishtank_launched.Notify();
                auto process = std::make_unique<MockManagedProcess>();
                MockManagedProcess* process_ptr = process.get();
                EXPECT_CALL(*process_ptr, GetPid()).WillRepeatedly(Return(5678));
                EXPECT_CALL(*process_ptr, Kill(SIGTERM)).Times(testing::AtMost(1));
                return absl::StatusOr<std::unique_ptr<::goldfish::async::ManagedProcess>>(
                        std::move(process));
            });

    ExpectEmulatorLaunch(emulator_launched, emulator_exit_cb);

    std::thread trigger = TriggerEmulatorScript(
            emulator_launched,
            [&]() {
                event_loop
                        ->Post([&]() {
                            if (fishtank_exit_cb) fishtank_exit_cb(0, 0);
                            emulator_exit_cb(0, 0);
                        })
                        .IgnoreError();
            },
            &fishtank_launched);

    RunLauncher(std::move(avd));

    trigger.join();
}

TEST_F(LauncherTest, DoesNotLaunchFishtankWhenWindowIsDisabled) {
    auto avd = CreateMockAvd();
    opts.no_window = true;

    // Verify fishtank is NOT launched.
    EXPECT_CALL(*mock_launcher, Launch(testing::Field(&::goldfish::async::LaunchConfig::exe_path,
                                                      emulator_paths.fishtank_binary),
                                       _))
            .Times(0);

    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;
    absl::Notification launched;
    ExpectEmulatorLaunch(launched, emulator_exit_cb);

    std::thread trigger = TriggerEmulatorExit(launched, emulator_exit_cb);

    RunLauncher(std::move(avd));
    trigger.join();
}

TEST_F(LauncherTest, HandlesEmulatorExitFailure) {
    auto avd = CreateMockAvd();

    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;
    absl::Notification launched;
    ExpectEmulatorLaunch(launched, emulator_exit_cb);

    std::thread trigger = TriggerEmulatorExit(launched, emulator_exit_cb, 1);

    int status = RunLauncher(std::move(avd));

    trigger.join();
    EXPECT_EQ(status, 1);
}

TEST_F(LauncherTest, ForwardsMultipleSignalsToEmulator) {
    auto avd = CreateMockAvd();

    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;
    ::goldfish::async::SignalHandlers::Callback launcher_signal_cb;
    absl::Notification launched;
    absl::Notification callback_set;
    MockManagedProcess* mock_process_ptr = nullptr;

    ExpectEmulatorLaunch(launched, emulator_exit_cb, &mock_process_ptr);

    EXPECT_CALL(*mock_signal_handlers, SetCallback(_))
            .WillOnce([&](::goldfish::async::SignalHandlers::Callback signal_cb) {
                launcher_signal_cb = std::move(signal_cb);
                callback_set.Notify();
            });

    std::thread trigger = TriggerEmulatorScript(
            launched,
            [&]() {
                EXPECT_CALL(*mock_process_ptr, Kill(SIGINT)).Times(1);
                EXPECT_CALL(*mock_process_ptr, Kill(SIGTERM)).Times(1);
                EXPECT_CALL(*mock_process_ptr, Kill(SIGHUP)).Times(1);

                event_loop->Post([&]() { launcher_signal_cb(SIGINT); }).IgnoreError();
                event_loop->Post([&]() { launcher_signal_cb(SIGTERM); }).IgnoreError();
                event_loop->Post([&]() { launcher_signal_cb(SIGHUP); }).IgnoreError();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                event_loop->Post([&]() { emulator_exit_cb(0, 0); }).IgnoreError();
            },
            &callback_set);

    RunLauncher(std::move(avd));

    trigger.join();
}

TEST_F(LauncherTest, HandlesPortOption) {
    auto avd = CreateMockAvd();
    opts.port = "5562";

    EXPECT_CALL(*mock_socket_factory, CreateServer(_, IsPort(5562), _, _))
            .Times(2)
            .WillRepeatedly(Return(std::make_shared<MockAsyncSocketServer>()));

    absl::Notification launched;
    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;
    ExpectEmulatorLaunch(launched, emulator_exit_cb);

    std::thread trigger = TriggerEmulatorExit(launched, emulator_exit_cb);

    RunLauncher(std::move(avd));
    trigger.join();
}

TEST_F(LauncherTest, HandlesValidPortsOption) {
    auto avd = CreateMockAvd();
    opts.ports = "5560,5561";

    EXPECT_CALL(*mock_socket_factory, CreateServer(_, IsPort(5560), _, _))
            .Times(2)
            .WillRepeatedly(Return(std::make_shared<MockAsyncSocketServer>()));

    absl::Notification launched;
    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;

    auto config_matcher = testing::AllOf(
            testing::Field(&::goldfish::async::LaunchConfig::exe_path, ExpectedQemuBinary()),
            testing::Field(
                    &::goldfish::async::LaunchConfig::args,
                    testing::AllOf(testing::Contains(testing::HasSubstr("avdstart")),
                                   testing::Contains(testing::HasSubstr("serial_number=5560")),
                                   testing::Contains(testing::HasSubstr("adb_port=5561")))));

    ExpectEmulatorLaunch(launched, emulator_exit_cb, nullptr, config_matcher);

    std::thread trigger = TriggerEmulatorExit(launched, emulator_exit_cb);

    RunLauncher(std::move(avd));

    trigger.join();
}

TEST_F(LauncherTest, HuntsForFreePort) {
    auto avd = CreateMockAvd();

    // Mock port 5554 and 5556 as busy, 5558 as free.
    EXPECT_CALL(*mock_socket_factory, CreateServer(_, IsPort(5554), _, _))
            .WillOnce(Return(nullptr));
    EXPECT_CALL(*mock_socket_factory, CreateServer(_, IsPort(5556), _, _))
            .WillOnce(Return(nullptr));
    EXPECT_CALL(*mock_socket_factory, CreateServer(_, IsPort(5558), _, _))
            .Times(2)
            .WillRepeatedly(Return(std::make_shared<MockAsyncSocketServer>()));

    absl::Notification launched;
    ::goldfish::async::ProcessLauncher::ExitCallback emulator_exit_cb;

    auto config_matcher = testing::AllOf(
            testing::Field(&::goldfish::async::LaunchConfig::exe_path, ExpectedQemuBinary()),
            testing::Field(
                    &::goldfish::async::LaunchConfig::args,
                    testing::AllOf(testing::Contains(testing::HasSubstr("avdstart")),
                                   testing::Contains(testing::HasSubstr("serial_number=5558")))));

    ExpectEmulatorLaunch(launched, emulator_exit_cb, nullptr, config_matcher);

    std::thread trigger = TriggerEmulatorExit(launched, emulator_exit_cb);

    RunLauncher(std::move(avd));

    trigger.join();
}

TEST_F(LauncherTest, ShutsDownDirectlyWhenNoEmulatorProcess) {
    auto avd = CreateMockAvd();

    ::goldfish::async::SignalHandlers::Callback launcher_signal_cb;
    absl::Notification callback_set;
    EXPECT_CALL(*mock_signal_handlers, SetCallback(_))
            .WillOnce([&](::goldfish::async::SignalHandlers::Callback signal_cb) {
                launcher_signal_cb = std::move(signal_cb);
                callback_set.Notify();
            });

    EXPECT_CALL(*mock_signal_handlers, close()).Times(1);

    // Ensure emulator is NEVER launched.
    EXPECT_CALL(*mock_launcher, Launch(_, _)).Times(0);

    absl::Notification port_hunt_blocked;
    absl::Notification port_hunt_can_continue;
    EXPECT_CALL(*mock_socket_factory, CreateServer(_, IsPort(5554), _, _))
            .WillOnce([&](auto, auto, auto, auto) {
                port_hunt_blocked.Notify();
                port_hunt_can_continue.WaitForNotification();
                return std::make_shared<MockAsyncSocketServer>();
            })
            .WillOnce(Return(std::make_shared<MockAsyncSocketServer>()));

    std::thread trigger = TriggerEmulatorScript(callback_set, [&]() {
        port_hunt_blocked.WaitForNotification();
        // Signal task is posted while port hunt is blocked.
        // It will be queued on the event loop and run after the current init task.
        event_loop->Post([&]() { launcher_signal_cb(SIGINT); }).IgnoreError();
        // Now let port hunt continue.
        port_hunt_can_continue.Notify();
    });

    int status = RunLauncher(std::move(avd));
    trigger.join();
    EXPECT_EQ(status, 0);
}

}  // namespace
}  // namespace android::goldfish
