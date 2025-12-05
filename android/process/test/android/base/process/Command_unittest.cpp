
// Copyright (C) 2022 The Android Open Source Project
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
#include "aemu/base/process/Command.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#else
#include <Windows.h>
#endif  // !_WIN32

#include "android/base/bazel/bazel_info.h"

namespace android {
namespace base {

// GTEST_FLAG(std::string, sleep_exe, 0, "Path to the sleep executable");

using namespace std::chrono_literals;
const std::string HELLO = "hello";

class FakeOverseer : public NullOverseer {
  public:
    void start(RingStreambuf* out, RingStreambuf* err) override {
        out->sputn(HELLO.c_str(), HELLO.size());
        out->close();
    }
};

#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

std::string sleep_exe() {
    return Bazel::runfilesPath(absl::StrCat("goldfish+/android/process/sleep_emu", EXE));
}

// You can always make your own fake commands..
class FakeProcess : public ObservableProcess {
  public:
    std::string exe() const override { return "Fake!"; }
    bool isAlive() const override { return false; }
    bool terminate() override { return true; }

    std::future_status wait_for_kernel(
            const std::chrono::milliseconds timeout_duration) const override {
        std::this_thread::sleep_for(std::min(10ms, timeout_duration));
        return std::future_status::ready;
    }

    std::optional<ProcessExitCode> getExitCode() const override { return 0; }
    std::optional<Pid> createProcess(const CommandArguments& args, bool deamon,
                                     bool replace) override {
        return 123;
    }
    std::unique_ptr<ProcessOverseer> createOverseer() override {
        return std::make_unique<FakeOverseer>();
    };
};

TEST(Process, find_me) {
    auto me = Process::me();
    EXPECT_NE(me, nullptr);
    EXPECT_GT(me->pid(), 0);
}

TEST(Process, discovered_proc_same_as_launched) {
    auto proc = Command::create({sleep_exe(), "--sleep", "1s"}).execute();
    auto sleep = Process::fromPid(proc->pid());
    ASSERT_NE(sleep, nullptr);
    EXPECT_EQ(proc->pid(), sleep->pid());
    EXPECT_EQ(*proc, *sleep);
}

TEST(Process, can_discover_launched_proc) {
    using namespace std::chrono_literals;
    auto proc = Command::create({sleep_exe(), "--sleep", "1s"}).execute();
    auto pids = Process::fromName("sleep_emu");

    auto now = std::chrono::system_clock::now();
    // On linux we scan /proc/... which is not instantenous on our gce machines.
    // Note that the scan itself can take +/- 20ms.
    while (pids.size() == 0 && std::chrono::system_clock::now() < now + 200ms) {
        pids = Process::fromName("sleep_emu");
    }
    LOG(INFO) << "It took "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now() - now)
                         .count()
              << " ms. to find the process";
    EXPECT_GT(pids.size(), 0);

    bool found = false;
    for (const auto& pid : pids) {
        if (pid->pid() == proc->pid()) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << sleep_exe() << "was not found.";
}

TEST(Process, can_read_process_name) {
    auto proc = Command::create({sleep_exe(), "--sleep", "1s"}).execute();
    std::this_thread::sleep_for(10ms);
    auto sleep = Process::fromPid(proc->pid());
    auto name = sleep->exe();
    EXPECT_TRUE(absl::StrContains(name, "sleep_emu"))
            << "Expected sleep_emu in the process name: " << name
            << ", are your running the test in the directory where sleep_emu "
               "is?";
}

TEST(Process, can_get_exitcode_from_discovered_process) {
    auto proc =
            Command::create({sleep_exe(), "--sleep", "200ms", "--exit", "2"}).asDeamon().execute();
    auto sleep = Process::fromPid(proc->pid());
    EXPECT_EQ(sleep->exitCode(), 2);
}

TEST(Process, terminate_someone_else) {
    auto proc = Command::create({sleep_exe(), "--sleep", "200ms"}).asDeamon().execute();
    auto sleep = Process::fromPid(proc->pid());
    sleep->terminate();
    EXPECT_FALSE(sleep->isAlive());
}

TEST(Command, can_use_test_factory) {
    int create_called = 0;
    Command::setTestProcessFactory([&](CommandArguments args, bool deamon, bool inherit) {
        create_called++;
        return std::make_unique<FakeProcess>();
    });

    auto proc = Command::create({"foo"}).withStdoutBuffer(4096).execute();
    EXPECT_EQ(create_called, 1);
    EXPECT_EQ(proc->exitCode(), 0);
    EXPECT_FALSE(proc->isAlive());
    EXPECT_EQ(proc->out()->asString(), HELLO);
    Command::setTestProcessFactory(nullptr);
}

TEST(Command, can_read_the_exit_code) {
    auto proc = Command::create({sleep_exe(), "--sleep", "10ms", "--exit", "2"}).execute();
    EXPECT_EQ(proc->exitCode(), 2U);
}

TEST(Command, properly_escape_params) {
    auto proc = Command::create({sleep_exe()})
                        .arg("--msg_std_out")
                        .arg("Hello there")
                        .withStdoutBuffer(4096, 10ms)
                        .execute();
    proc->wait_for(100ms);
    EXPECT_EQ(proc->out()->asString(), "Hello there");
}

TEST(Command, a_terminated_process_is_dead) {
    using namespace std::chrono_literals;
    auto proc = Command::create({sleep_exe(), "--sleep", "5s"}).execute();
    EXPECT_TRUE(proc->isAlive());
    EXPECT_TRUE(proc->terminate());
    EXPECT_FALSE(proc->isAlive());
}

TEST(Command, out_of_scope_process_gets_terminated) {
    int pid = 0;
    {
        auto proc = Command::create({sleep_exe(), "--sleep", "5s"}).execute();
        EXPECT_TRUE(proc->isAlive());
        pid = proc->pid();
    }

    EXPECT_GT(pid, 0);
    EXPECT_FALSE(Process::fromPid(pid)->isAlive());
}

TEST(Command, wait_for_completion_times_out) {
    auto proc = Command::create({sleep_exe(), "--sleep", "5s"}).execute();

    // Well, we sleep for a few seconds.. so we should timeout.
    EXPECT_EQ(proc->wait_for(10ms), std::future_status::timeout);
    EXPECT_TRUE(proc->isAlive());
}

TEST(Command, we_can_capture_std_out) {
    // Let's capture std out
    auto proc = Command::create({sleep_exe(), "--msg_std_out", "stdout"})
                        .withStdoutBuffer(4096)
                        .execute();
    proc->wait_for(1s);
    std::this_thread::sleep_for(10ms);

    // We should print out the message.
    EXPECT_EQ(proc->out()->asString(), "stdout");
    EXPECT_EQ(proc->err()->asString(), "");
}

TEST(Command, we_can_capture_std_err) {
    // Let's capture std err
    auto proc = Command::create({sleep_exe(), "--msg_std_err", "error"})
                        .withStderrBuffer(4096)
                        .execute();
    proc->wait_for(1s);
    std::this_thread::sleep_for(10ms);

    // We should print out the message.
    EXPECT_EQ(proc->err()->asString(), "error");
}

void clearCloseOnExec(FILE* sharedFile) {
#ifndef _WIN32
    int fd = fileno(sharedFile);
    auto flags = fcntl(fd, F_GETFD);
    flags &= ~FD_CLOEXEC;  // Clear the close-on-exec flag.
    fcntl(fd, F_SETFD, flags);
#endif  // !_WIN32
}

TEST(Command, we_can_capture_both) {
    // Let's capture std err
    auto proc = Command::create({sleep_exe(), "--msg_std_out", "stdout", "--msg_std_err", "error"})
                        .withStdoutBuffer(4096)
                        .withStderrBuffer(4096)
                        .execute();
    proc->wait_for(200ms);
    std::this_thread::sleep_for(10ms);

    // We should print out the message.
    EXPECT_EQ(proc->out()->asString(), "stdout");
    EXPECT_EQ(proc->err()->asString(), "error");
}

TEST(Command, double_capture_should_not_lock) {
    // Let's capture std err
    auto proc = Command::create({sleep_exe(), "--msg_std_out", "stdout", "--msg_std_err", "error"})
                        .withStderrBuffer(4096)
                        .execute();
    proc->wait_for(1s);
    std::this_thread::sleep_for(10ms);

    // We should print out the message.
    EXPECT_EQ(proc->err()->asString(), "error");
}

TEST(Command, can_terminate_daemon) {
    auto proc = Command::create({sleep_exe()}).asDeamon().execute();

    // Well, we sleep for a few seconds.. so we should timeout.
    EXPECT_TRUE(proc->isAlive());
    EXPECT_TRUE(proc->terminate());
    EXPECT_FALSE(proc->isAlive());
}

}  // namespace base
}  // namespace android
