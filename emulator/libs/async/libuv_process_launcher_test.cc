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

#include "goldfish/async/libuv_process_launcher.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <thread>

#include "absl/base/log_severity.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "android/base/testing/test_temp_dir.h"
#include "goldfish/async/launch_config.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/process_launcher.h"
#include "goldfish/file/file.h"

namespace goldfish::async {
namespace {

using ::testing::_;
using ::testing::HasSubstr;

constexpr absl::Duration kExitTimeout = absl::Seconds(10);

#ifdef _WIN32
constexpr std::string_view kExeSuffix = ".exe";
#else
constexpr std::string_view kExeSuffix = "";
#endif

std::filesystem::path GetSleepExePath() {
    if (android::base::Bazel::InBazel()) {
        return std::filesystem::path(android::base::Bazel::RunfilesPath(
                absl::StrCat("goldfish+/emulator/libs/process/sleep_emu", kExeSuffix)));
    }
    return std::filesystem::path(android::base::System::Get()->EnvGet("SLEEP_EMU_PATH"));
}

TEST(LibuvProcessLauncherTest, PipedChildDestroyedAfterLoopShutdownClosesCleanly) {
    const std::filesystem::path sleep_exe = GetSleepExePath();
    ASSERT_FALSE(sleep_exe.empty());
    ASSERT_TRUE(android::base::file::exists(sleep_exe));

    android::base::TestTempDir temp_dir("process_launcher_test");
    const std::filesystem::path stdout_path =
            temp_dir.MakeSubPath(std::filesystem::path(std::u8string_view(u8"stdout-テスト.log")));

    auto loop = LibuvEventLoop::Create("ProcessLauncherTest");
    auto launcher = std::make_unique<UvProcessLauncher>(*loop);
    std::thread runner([&]() { (void)loop->Run(); });

    absl::Notification exited;
    std::atomic<int64_t> exit_code{-1};

    absl::StatusOr<std::unique_ptr<ManagedProcess>> child = loop->PostAndWait([&]() {
        LaunchConfig config;
        config.exe_path = sleep_exe;
        config.args = {"--sleep=10ms", "--msg_std_out=hello-テスト"};
        config.daemon = false;
        config.stdio_mode = LaunchConfig::StdioMode::kPipe;
        config.pipe_stdout_path = stdout_path;
        return launcher->Launch(config, [&](int64_t status, int /*term_signal*/) {
            exit_code.store(status);
            exited.Notify();
        });
    });
    ASSERT_THAT(child, absl_testing::IsOk());

    ASSERT_TRUE(exited.WaitForNotificationWithTimeout(kExitTimeout));
    EXPECT_EQ(exit_code.load(), 0);

    ASSERT_THAT(loop->ShutdownAndWait(), absl_testing::IsOk());
    runner.join();

    // Mirror Launcher teardown order: ManagedProcess and UvProcessLauncher are
    // destroyed after Run() has exited, then LibuvEventLoop is destroyed.
    child->reset();
    launcher.reset();
    EXPECT_TRUE(android::base::file::exists(stdout_path));

    absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
    EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, _, HasSubstr("Failed to close uv_loop")))
            .Times(0);
    log.StartCapturingLogs();

    loop.reset();

    log.StopCapturingLogs();
}

TEST(LibuvProcessLauncherTest, DetachedProcessDestroyedAfterLoopShutdownClosesCleanly) {
    const std::filesystem::path sleep_exe = GetSleepExePath();
    ASSERT_FALSE(sleep_exe.empty());
    ASSERT_TRUE(android::base::file::exists(sleep_exe));

    auto loop = LibuvEventLoop::Create("DetachedProcessTest");
    auto launcher = std::make_unique<UvProcessLauncher>(*loop);
    std::thread runner([&]() { (void)loop->Run(); });

    absl::StatusOr<std::unique_ptr<ManagedProcess>> child =
            loop->PostAndWait([&]() -> absl::StatusOr<std::unique_ptr<ManagedProcess>> {
                LaunchConfig config;
                config.exe_path = sleep_exe;
                config.args = {"--sleep=50ms"};
                config.daemon = true;
                config.stdio_mode = LaunchConfig::StdioMode::kPipe;
                auto process = launcher->Launch(config, [](int64_t /*status*/, int /*signal*/) {});
                if (process.ok()) {
                    launcher->ForgetProcess(**process);
                }
                return process;
            });
    ASSERT_THAT(child, absl_testing::IsOk());

    ASSERT_THAT(loop->ShutdownAndWait(), absl_testing::IsOk());
    runner.join();

    child->reset();
    launcher.reset();

    absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
    EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, _, HasSubstr("Failed to close uv_loop")))
            .Times(0);
    log.StartCapturingLogs();

    loop.reset();

    log.StopCapturingLogs();
}

TEST(LibuvProcessLauncherTest, LoopDestroyedBeforeDetachedProcessAndLauncherClosesCleanly) {
    const std::filesystem::path sleep_exe = GetSleepExePath();
    ASSERT_FALSE(sleep_exe.empty());
    ASSERT_TRUE(android::base::file::exists(sleep_exe));

    auto loop = LibuvEventLoop::Create("ReverseTeardownTest");
    auto launcher = std::make_unique<UvProcessLauncher>(*loop);
    std::thread runner([&]() { (void)loop->Run(); });

    absl::StatusOr<std::unique_ptr<ManagedProcess>> child =
            loop->PostAndWait([&]() -> absl::StatusOr<std::unique_ptr<ManagedProcess>> {
                LaunchConfig config;
                config.exe_path = sleep_exe;
                config.args = {"--sleep=50ms"};
                config.daemon = true;
                config.stdio_mode = LaunchConfig::StdioMode::kPipe;
                auto process = launcher->Launch(config, [](int64_t /*status*/, int /*signal*/) {});
                if (process.ok()) {
                    launcher->ForgetProcess(**process);
                }
                return process;
            });
    ASSERT_THAT(child, absl_testing::IsOk());

    ASSERT_THAT(loop->ShutdownAndWait(), absl_testing::IsOk());
    runner.join();

    absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
    EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, _, HasSubstr("Failed to close uv_loop")))
            .Times(0);
    log.StartCapturingLogs();

    loop.reset();

    log.StopCapturingLogs();

    child->reset();
    launcher.reset();
}

}  // namespace
}  // namespace goldfish::async
