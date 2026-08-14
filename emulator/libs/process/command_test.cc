
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
#include "android/process/command.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#else
#include <Windows.h>
#undef CreateProcess
#endif  // !_WIN32

#include "android/base/bazel_info.h"

namespace android {
namespace base {

// GTEST_FLAG(std::string, sleep_exe, 0, "Path to the sleep executable");

using namespace std::chrono_literals;
namespace fs = std::filesystem;
const std::string kHello = "hello";

class FakeOverseer : public NullOverseer {
  public:
    void Start(std::basic_streambuf<char>* out, std::basic_streambuf<char>* err) override {
        out->sputn(kHello.c_str(), kHello.size());
    }
};

#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

std::string SleepExe() {
    return Bazel::RunfilesPath(absl::StrCat("goldfish+/emulator/libs/process/sleep_emu", EXE));
}

// You can always make your own fake commands..
class FakeProcess : public ObservableProcess {
  public:
    std::string Exe() const override { return "Fake!"; }
    bool IsAlive() const override { return false; }
    bool Terminate() override { return true; }

    std::future_status WaitForKernel(
            const std::chrono::milliseconds timeout_duration) const override {
        std::this_thread::sleep_for(std::min(10ms, timeout_duration));
        return std::future_status::ready;
    }

    std::optional<ProcessExitCode> GetExitCode() const override { return 0; }
    std::optional<Pid> CreateProcess(const CommandArguments& args, bool capture_output,
                                     bool replace) override {
        return 123;
    }
    std::unique_ptr<ProcessOverseer> CreateOverseer() override {
        return std::make_unique<FakeOverseer>();
    };
};

TEST(Process, find_me) {
    auto me = Process::Me();
    EXPECT_NE(me, nullptr);
    EXPECT_GT(me->pid(), 0);
}

TEST(Process, discovered_proc_same_as_launched) {
    auto proc = Command::Create({SleepExe(), "--sleep", "1s"}).Execute();
    auto sleep = Process::FromPid(proc->pid());
    ASSERT_NE(sleep, nullptr);
    EXPECT_EQ(proc->pid(), sleep->pid());
    EXPECT_EQ(*proc, *sleep);
}

TEST(Process, can_discover_launched_proc) {
    auto proc = Command::Create({SleepExe(), "--sleep", "1s"}).Execute();
    auto pids = Process::FromName("sleep_emu");

    const absl::Time start = absl::Now();
    const absl::Time deadline = start + absl::Seconds(2);
    // On linux we scan /proc/... which is not instantenous on our gce machines.
    // Note that the scan itself can take +/- 20ms.
    while (pids.size() == 0 && absl::Now() < deadline) {
        pids = Process::FromName("sleep_emu");
    }
    LOG(INFO) << "It took " << (absl::Now() - start) << " to find the process";
    EXPECT_GT(pids.size(), 0);

    bool found = false;
    for (const auto& pid : pids) {
        if (pid->pid() == proc->pid()) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << SleepExe() << "was not found.";
}

TEST(Process, can_read_process_name) {
    auto proc = Command::Create({SleepExe(), "--sleep", "1s"}).Execute();
    auto sleep = Process::FromPid(proc->pid());
    ASSERT_NE(sleep, nullptr);
    std::string name;
    const absl::Time deadline = absl::Now() + absl::Seconds(2);
    while (name.empty() && absl::Now() < deadline) {
        name = sleep->Exe();
    }
    EXPECT_TRUE(absl::StrContains(name, "sleep_emu"))
            << "Expected sleep_emu in the process name: " << name
            << ", are your running the test in the directory where sleep_emu "
               "is?";
}

TEST(Process, can_get_exitcode_from_discovered_process) {
    auto proc =
            Command::Create({SleepExe(), "--sleep", "200ms", "--exit", "2"}).Asdaemon().Execute();
    auto sleep = Process::FromPid(proc->pid());
    ASSERT_NE(sleep, nullptr);
    EXPECT_EQ(sleep->ExitCode(), 2);
}

TEST(Process, terminate_someone_else) {
    auto proc = Command::Create({SleepExe(), "--sleep", "200ms"}).Asdaemon().Execute();
    auto sleep = Process::FromPid(proc->pid());
    ASSERT_NE(sleep, nullptr);
    sleep->Terminate();
    EXPECT_FALSE(sleep->IsAlive());
}

TEST(Command, can_use_test_factory) {
    std::basic_stringbuf<char> std_out;
    std::basic_stringbuf<char> std_err;

    int create_called = 0;
    Command::SetTestProcessFactory([&](CommandArguments args, bool daemon, bool inherit) {
        create_called++;
        return std::make_unique<FakeProcess>();
    });

    auto proc = Command::Create({"foo"}).RedirectStdoutToUnsafe(&std_out).Execute();
    EXPECT_EQ(create_called, 1);
    EXPECT_EQ(proc->ExitCode(), 0);
    EXPECT_FALSE(proc->IsAlive());
    EXPECT_EQ(proc->Out()->AsString(), kHello);
    Command::SetTestProcessFactory(nullptr);
}

TEST(Command, can_read_the_exit_code) {
    auto proc = Command::Create({SleepExe(), "--sleep", "10ms", "--exit", "2"}).Execute();
    EXPECT_EQ(proc->ExitCode(), 2U);
}

TEST(Command, properly_escape_params) {
    std::basic_stringbuf<char> std_out;
    auto proc = Command::Create({SleepExe()})
                        .Arg("--msg_std_out")
                        .Arg("Hello there")
                        .RedirectStdoutToUnsafe(&std_out)
                        .Execute();
    ASSERT_EQ(proc->WaitFor(500ms), std::future_status::ready);
    EXPECT_EQ(proc->Out()->AsString(), "Hello there");
}

TEST(Command, a_terminated_process_is_dead) {
    using namespace std::chrono_literals;
    auto proc = Command::Create({SleepExe(), "--sleep", "5s"}).Execute();
    EXPECT_TRUE(proc->IsAlive());
    EXPECT_TRUE(proc->Terminate());
    EXPECT_FALSE(proc->IsAlive());
}

TEST(Command, out_of_scope_process_gets_terminated) {
    int pid = 0;
    {
        auto proc = Command::Create({SleepExe(), "--sleep", "5s"}).Execute();
        EXPECT_TRUE(proc->IsAlive());
        pid = proc->pid();
    }

    EXPECT_GT(pid, 0);
    auto dead_proc = Process::FromPid(pid);
    if (dead_proc) {
        EXPECT_FALSE(dead_proc->IsAlive());
    }
}

TEST(Command, WaitFor_completion_times_out) {
    auto proc = Command::Create({SleepExe(), "--sleep", "5s"}).Execute();

    // Well, we sleep for a few seconds.. so we should timeout.
    EXPECT_EQ(proc->WaitFor(10ms), std::future_status::timeout);
    EXPECT_TRUE(proc->IsAlive());
}

TEST(Command, we_can_capture_std_out) {
    std::basic_stringbuf<char> std_out;
    // Let's capture std out
    auto proc = Command::Create({SleepExe(), "--msg_std_out", "stdout"})
                        .RedirectStdoutToUnsafe(&std_out)
                        .Execute();
    ASSERT_EQ(proc->WaitFor(500ms), std::future_status::ready);

    // We should print out the message.
    EXPECT_EQ(proc->Out()->AsString(), "stdout");
    EXPECT_EQ(proc->Err()->AsString(), "");
}

TEST(Command, we_can_capture_std_err) {
    std::basic_stringbuf<char> std_err;
    // Let's capture std err
    auto proc = Command::Create({SleepExe(), "--msg_std_err", "error"})
                        .RedirectStderrToUnsafe(&std_err)
                        .Execute();
    ASSERT_EQ(proc->WaitFor(500ms), std::future_status::ready);

    // We should print out the message.
    EXPECT_EQ(proc->Err()->AsString(), "error");
}

void ClearCloseOnExec(FILE* shared_file) {
#ifndef _WIN32
    int fd = fileno(shared_file);
    auto flags = fcntl(fd, F_GETFD);
    flags &= ~FD_CLOEXEC;  // Clear the close-on-exec flag.
    fcntl(fd, F_SETFD, flags);
#endif  // !_WIN32
}

// TODO(whollins): Fix these 2 tests to use a different type of FD.
TEST(Command, DISABLED_we_do_not_inherit_handles) {
    // Let's capture std err
    /*std::string tmp_file = std::tmpnam(nullptr);

    auto share_mode = android::base::FileShare::Write;
    android::base::createFileForShare(tmp_file.c_str());
    const char* mode = "wb";
    FILE* shared_file = android::base::fsopen(tmp_file.c_str(), mode, share_mode);
    ClearCloseOnExec(shared_file);

    auto proc = Command::Create({SleepExe(), "--sleep", "5s"}).Execute();
    std::this_thread::sleep_for(10ms);
#ifndef _WIN32
    android::base::internal::closeFileForShare(shared_file);
#else
    _close(fileno(shared_file));
#endif
    shared_file = nullptr;
    shared_file = android::base::fsopen(tmp_file.c_str(), mode, share_mode);
    EXPECT_TRUE(shared_file != nullptr && proc->IsAlive())
            << "The file handle should not have been inherited and not be null, not: " <<
shared_file
            << (proc->IsAlive() ? " proc is and should be alive!" : "should not be dead");

    // Let's make sure we do not have any weird dangling file descriptors.
    proc->Terminate();
#ifndef _WIN32
    android::base::internal::closeFileForShare(shared_file);
#else
    _close(fileno(shared_file));
#endif*/
}

TEST(Command, DISABLED_we_do_inherit_handles_if_we_explicitly_say_so) {
    // Let's capture std err
    /*std::string tmp_file = std::tmpnam(nullptr);

    auto share_mode = android::base::FileShare::Write;
    android::base::createFileForShare(tmp_file.c_str());
    const char* mode = "wb";
    FILE* shared_file = android::base::fsopen(tmp_file.c_str(), mode, share_mode);
    ClearCloseOnExec(shared_file);

    auto proc = Command::Create({SleepExe(), "--sleep", "5s"}).Inherit().Execute();
    std::this_thread::sleep_for(10ms);

#ifndef _WIN32
    android::base::internal::closeFileForShare(shared_file);
#else
    _close(fileno(shared_file));
#endif
    shared_file = android::base::fsopen(tmp_file.c_str(), mode, share_mode);
    EXPECT_TRUE(shared_file == nullptr && proc->IsAlive())
            << "The file handle should have been inherited and be null, not: " << shared_file
            << (proc->IsAlive() ? " proc is and should be alive!" : "should not be dead");

    proc->Terminate();*/
}

TEST(Command, we_can_capture_both) {
    std::basic_stringbuf<char> std_out;
    std::basic_stringbuf<char> std_err;
    // Let's capture std err
    auto proc = Command::Create({SleepExe(), "--msg_std_out", "stdout", "--msg_std_err", "error"})
                        .RedirectStdoutToUnsafe(&std_out)
                        .RedirectStderrToUnsafe(&std_err)
                        .Execute();
    ASSERT_EQ(proc->WaitFor(500ms), std::future_status::ready);

    // We should print out the message.
    EXPECT_EQ(proc->Out()->AsString(), "stdout");
    EXPECT_EQ(proc->Err()->AsString(), "error");
}

TEST(Command, double_capture_should_not_lock) {
    std::basic_stringbuf<char> std_err;
    // Let's capture std err
    auto proc = Command::Create({SleepExe(), "--msg_std_out", "stdout", "--msg_std_err", "error"})
                        .RedirectStderrToUnsafe(&std_err)
                        .Execute();
    ASSERT_EQ(proc->WaitFor(500ms), std::future_status::ready);

    // We should print out the message.
    EXPECT_EQ(proc->Err()->AsString(), "error");
}

TEST(Command, can_terminate_daemon) {
    auto proc = Command::Create({SleepExe()}).Asdaemon().Execute();

    // Well, we sleep for a few seconds.. so we should timeout.
    EXPECT_TRUE(proc->IsAlive());
    EXPECT_TRUE(proc->Terminate());
    EXPECT_FALSE(proc->IsAlive());
}

// Note this a bit slow
TEST(Command, DISABLED_we_can_stream_data) {
    std::basic_stringbuf<char> std_out;
#ifndef _WIN32
    auto cmd = Command::Create({"sh", "-c"});
#else
    auto cmd = Command::Create({"cmd.exe", "/C"});
#endif

    // An example of streaming data, note if we do not receive
    // data every second we will consider the stream closed!
    auto proc = cmd.Arg(R"##(for i in {1..2}; do echo "Hello $i"; sleep 0.2; done)##")
                        .RedirectStdoutToUnsafe(&std_out)
                        .Execute();

    int i = 1;

    // You can read from the stream, if the process goes awat
    // the stream will close (and no longer be good)
    std::istream& stream = proc->Out()->AsStream();
    while (stream.good()) {
        // Pull a line from the stream..
        char buffer[80];
        stream.getline(buffer, sizeof(buffer));
        std::string line(buffer);

        // Note, last line will be empty..
        if (!line.empty()) ASSERT_EQ("Hello " + std::to_string(i++), line);
    }

    // Let's not have a dangling shell.
    proc->Terminate();
}

TEST(Command, reports_signal_exit_code) {
#ifdef _WIN32
    GTEST_SKIP() << "Signals are handled differently on Windows";
#endif
    // SIGSEGV is 11, so exit code should be 128 + 11 = 139.
    auto proc = Command::Create({"sh", "-c", "kill -SEGV $$"}).Execute();
    EXPECT_EQ(proc->ExitCode(), 139U);
    EXPECT_FALSE(proc->IsAlive());
}

TEST(Command, as_string_does_not_hang_on_crash) {
    std::basic_stringbuf<char> std_out;
#ifdef _WIN32
    // TODO Fix this.
    GTEST_SKIP() << "Currently posix only test.";
#endif
    // Let's capture std out of a process that crashes.
    auto proc = Command::Create({"sh", "-c", "echo hello; sleep 0.1; kill -SEGV $$"})
                        .RedirectStdoutToUnsafe(&std_out)
                        .Execute();

    // WaitFor should return once the process crashes and the overseer finishes.
    // If it hangs, the test will timeout.
    ASSERT_EQ(proc->WaitFor(5s), std::future_status::ready);

    EXPECT_FALSE(proc->IsAlive());
    EXPECT_EQ(proc->Out()->AsString(), "hello\n");
}

TEST(Command, can_capture_output_when_one_pipe_closes_early) {
    std::basic_stringbuf<char> std_out;
    std::basic_stringbuf<char> std_err;
#ifdef _WIN32
    // TODO Fix this.
    GTEST_SKIP() << "Currently posix only test.";
#endif
    // This closes stdout but keeps stderr open.
    auto proc = Command::Create({"sh", "-c", "echo stdout; exec 1>&-; sleep 0.1; echo stderr >&2"})
                        .RedirectStdoutToUnsafe(&std_out)
                        .RedirectStderrToUnsafe(&std_err)
                        .Execute();

    EXPECT_EQ(proc->WaitFor(500ms), std::future_status::ready);
    EXPECT_EQ(proc->Out()->AsString(), "stdout\n");
    EXPECT_EQ(proc->Err()->AsString(), "stderr\n");
}

TEST(Command, detach_keeps_process_alive) {
    android::base::Pid pid;
    {
        // Start a long running process
        auto proc = Command::Create({SleepExe(), "--sleep", "10s"}).Execute();
        // Detach should stop the overseer immediately.
        pid = proc->pid();
        proc->Detach();
        // proc goes out of scope here.
        // ~ObservableProcess will join the overseer thread.
        // If Detach() (Stop()) didn't wake up the poll loop, this join will wait for 10s.
    }
    auto proc = Process::FromPid(pid);
    EXPECT_TRUE(proc->IsAlive());
    proc->Terminate();
    EXPECT_FALSE(proc->IsAlive());
}

TEST(Command, detach_stops_overseer_immediately) {
    if (!Bazel::InBazel()) {
        GTEST_SKIP() << "This test can only be run under Bazel";
    }
    std::basic_stringbuf<char> std_out;
    std::basic_stringbuf<char> std_err;

    std::chrono::steady_clock::time_point start;
    {
        // Start a crashing process that outputs a lot to stdout, so if the overseer is still
        // running after detach, it should not hang. Without the fix, this test can hang
        // indefinetely (to see this run this test on repeat with -gtest_repeat=-1, without the fix)
        fs::path executable =
                Bazel::RunfilesPath(absl::StrCat("goldfish+/emulator/crashreport/crash-me", EXE));
        ASSERT_TRUE(fs::exists(executable))
                << "The crash-me executable does not exist at " << executable;

        auto proc = Command::Create({executable.string(), "--delay_ms", "0"})
                            .Inherit()
                            .RedirectStderrToUnsafe(&std_err)
                            .RedirectStdoutToUnsafe(&std_out)
                            .Execute();

        start = std::chrono::steady_clock::now();
        // Detach should stop the overseer immediately.
        proc->Detach();
    }
    auto end = std::chrono::steady_clock::now();
    // It should be instant.
    EXPECT_LT(end - start, 500ms);
}

}  // namespace base
}  // namespace android
