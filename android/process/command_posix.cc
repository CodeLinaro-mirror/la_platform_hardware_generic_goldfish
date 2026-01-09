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
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstring>
#include <future>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"

#include "aemu/base/EintrWrapper.h"
#include "android/base/file/file.h"
#include "android/process/command.h"
#include "android/process/exec.h"

#define DEBUG 0

#if DEBUG >= 1
#define DD(fmt, ...)                                                    \
    printf("%lld %s:%d %s| " fmt "\n",                                  \
           std::chrono::duration_cast<std::chrono::milliseconds>(       \
                   std::chrono::system_clock::now().time_since_epoch()) \
                   .count(),                                            \
           __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

#ifdef __APPLE__
#include <crt_externs.h>
#include <libproc.h>
#define environ (*_NSGetEnviron())
#else
#include <filesystem>
#endif

#ifndef _WIN32
#include <fcntl.h>
#endif  // !_WIN32

namespace android::base {

namespace {

std::vector<char*> ToCharArray(const std::vector<std::string>& params) {
    std::vector<char*> args;
    args.reserve(params.size());
    for (const auto& param : params) {
        args.push_back(const_cast<char*>(param.c_str()));
    }
    args.push_back(nullptr);
    return args;
}

#ifndef __APPLE__
std::string ReadProcLinux(int pid) {
    // parse out /proc/xx/cmdline
    const std::string proc = "/proc/" + std::to_string(pid) + "/cmdline";
    DD("Looking for commandline in %s", proc.c_str());

    std::string name;
    FILE* proc_file;
    int ch;

    // Opening file in reading mode
    proc_file = fopen(proc.c_str(), "r");
    if (!proc_file) {
        DD("Warning, cannot find proc.");
        return "";
    }

    // /proc likely will not report file size, so read it!
    while ((ch = fgetc(proc_file)) != EOF) {
        if (ch == 0) {
            fclose(proc_file);
            return name;
        }
        name += static_cast<char>(ch);
    }
    fclose(proc_file);
    return name;
}
#endif
}  // namespace

class PosixOverseer : public ProcessOverseer {
  public:
    PosixOverseer(const int std_out[2], const int std_err[2]) {
        std_out_pipe_[0] = std_out[0];
        std_out_pipe_[1] = std_out[1];
        std_err_pipe_[0] = std_err[0];
        std_err_pipe_[1] = std_err[1];
    }

    ~PosixOverseer() override { DD("~PosixOverseer"); }

    static void ReadAndFlush(int fd, std::basic_streambuf<char>* buffer) {
        if (buffer == nullptr) {
            return;
        }
        char bytes[1024];
        auto bytes_read = read(fd, bytes, sizeof(bytes));
        buffer->sputn(bytes, bytes_read);
        buffer->pubsync();
    }

    void Start(std::basic_streambuf<char>* out, std::basic_streambuf<char>* err) override {
        std::vector<pollfd> plist = {{.fd = std_out_pipe_[0], .events = POLLIN},
                                     {.fd = std_err_pipe_[0], .events = POLLIN}};
        int rval;
        while ((rval = poll(plist.data(), plist.size(),
                            /*timeout*/ -1)) > 0) {
            DD("Event!");
            if (plist[0].revents & POLLIN) {
                // out..
                ReadAndFlush(std_out_pipe_[0], out);
            } else if (plist[1].revents & POLLIN) {
                ReadAndFlush(std_err_pipe_[0], err);
            }

            // Detect if we have closed the pipes.
            auto revents = plist[0].revents;
            if (revents & POLLHUP || revents & POLLNVAL || revents & POLLERR) {
                break;
            }

            revents = plist[1].revents;
            if (revents & POLLHUP || revents & POLLNVAL || revents & POLLERR) {
                break;
            }
        }

        // Push out any left overs.
        ReadAndFlush(std_out_pipe_[0], out);
        ReadAndFlush(std_err_pipe_[0], err);
        DD("Observer finished.");
    }

    // Cancel the observation of the process, no callbacks
    // should be invoked.
    // no writes to std_out, std_err should happen.
    void Stop() override {
        close(std_out_pipe_[0]);
        close(std_err_pipe_[0]);
    };

  private:
    int std_out_pipe_[2];
    int std_err_pipe_[2];
};

class PosixProcess : public ObservableProcess {
  public:
    explicit PosixProcess(Pid pid) {
        pid_ = pid;
        daemon_ = true;
    }

    PosixProcess(bool daemon, bool inherit) {
        inherit_ = inherit;
        daemon_ = daemon;
    }

    ~PosixProcess() override {
        if (actions_) {
            posix_spawn_file_actions_destroy(actions_);
        }
        if (attr_) {
            posix_spawnattr_destroy(attr_);
        }
        if (!daemon_) PosixProcess::Terminate();
    }

    bool Terminate() override {
        using namespace std::chrono_literals;

        if (IsAlive()) {
            kill(pid_, SIGKILL);
            HANDLE_EINTR(waitpid(pid_, nullptr, WNOHANG));
            WaitForKernel(10s);
        }

        return !IsAlive();
    }

    bool IsAlive() const override {
        // Acknowledge process in case it is a zombie..
        GetExitCode();
        return kill(pid_, 0) == 0;
    }

    std::future_status WaitForKernel(
            const std::chrono::milliseconds timeout_duration) const override {
        using namespace std::chrono_literals;
        if (pid_ == -1) return std::future_status::ready;

        auto wait_until = std::chrono::system_clock::now() + timeout_duration;
        while (std::chrono::system_clock::now() < wait_until && IsAlive()) {
            std::this_thread::sleep_for(10ms);
            DD("Awakened, ready to check again.");
        }

        return std::chrono::system_clock::now() < wait_until ? std::future_status::ready
                                                             : std::future_status::timeout;
    }

    std::string Exe() const override {
#ifdef __APPLE__
        char name[PROC_PIDPATHINFO_MAXSIZE] = {0};
        proc_pidpath(pid_, name, sizeof(name));
#else
        std::string name = ReadProcLinux(pid_);
#endif
        return name;
    }

    std::optional<ProcessExitCode> GetExitCode() const override {
        if (process_exit_.has_value()) {
            return process_exit_;
        }

        ProcessExitCode exit_code;
        auto wait_pid = HANDLE_EINTR(waitpid(pid_, &exit_code, WNOHANG));
        if (wait_pid > 0) process_exit_ = WEXITSTATUS(exit_code);

        return process_exit_;
    };

    std::optional<Pid> CreateProcess(const CommandArguments& cmdline, bool capture_output,
                                     bool replace) override {
        // Setup the arguments..
        std::vector<char*> args = ToCharArray(cmdline);

        if (replace) {
            // The exec() functions only return if an error has occurred.
            SafeExecv(args[0], args.data());
            return std::nullopt;
        }

        DD("%s to inheriting handles..", mInherit ? "yes" : "no");
        if (!inherit_) {
            attr_ = new posix_spawnattr_t;
            if (posix_spawnattr_init(attr_)) {
                DD("Unable to initialize spawnattr..");
                return std::nullopt;
            }
#ifdef __APPLE__
            if (posix_spawnattr_setflags(attr_, POSIX_SPAWN_CLOEXEC_DEFAULT)) {
                DD("Failed to request CLOEXEC.");
                return std::nullopt;
            }
#else
            // We need to mark all file handles as close on exec.
            const int fdlimit = static_cast<int>(sysconf(_SC_OPEN_MAX));
            DD("Marking %d as close on exec", fdlimit);
            for (int i = STDERR_FILENO + 1; i < fdlimit; i++) {
                const int f = ::fcntl(i, F_GETFD);
                ::fcntl(i, F_SETFD, f | FD_CLOEXEC);
            }
            DD("Marked %d as close on exec -- done", fdlimit);
#endif
        }

        actions_ = new posix_spawn_file_actions_t;
        auto* action = actions_;
        posix_spawn_file_actions_init(action);

        if (capture_output) {
            if (pipe(std_out_pipe_) || pipe(std_err_pipe_)) {
                PLOG(WARNING) << "Unable to create pipes to connect to process:";
                return std::nullopt;
            }

            posix_spawn_file_actions_addclose(action, std_out_pipe_[0]);
            posix_spawn_file_actions_addclose(action, std_err_pipe_[0]);
            posix_spawn_file_actions_adddup2(action, std_out_pipe_[1], STDOUT_FILENO);
            posix_spawn_file_actions_adddup2(action, std_err_pipe_[1], STDERR_FILENO);

            posix_spawn_file_actions_addclose(action, std_out_pipe_[1]);
            posix_spawn_file_actions_addclose(action, std_err_pipe_[1]);
        } else {
            posix_spawn_file_actions_addopen(action, STDOUT_FILENO, "/dev/null", O_WRONLY, 0644);
            posix_spawn_file_actions_addopen(action, STDERR_FILENO, "/dev/null", O_WRONLY, 0644);
        }

        pid_t pid;
        auto error_code =
                posix_spawnp(&pid, cmdline[0].c_str(), action, attr_, args.data(), environ);
        if (error_code) {
            PLOG(ERROR) << "Unable to spawn process " << cmdline[0]
                        << " due to: " << strerror(error_code);
            return std::nullopt;
        }

        if (capture_output) {
            close(std_out_pipe_[1]);
            close(std_err_pipe_[1]);
        }

        return pid;
    }

    std::unique_ptr<ProcessOverseer> CreateOverseer() override {
        return std::make_unique<PosixOverseer>(std_out_pipe_, std_err_pipe_);
    }

  private:
    mutable std::optional<ProcessExitCode> process_exit_;

    int std_out_pipe_[2];
    int std_err_pipe_[2];
    posix_spawn_file_actions_t* actions_{nullptr};
    posix_spawnattr_t* attr_{nullptr};
};

Command::ProcessFactory Command::s_process_factory = [](const CommandArguments& /* args */,
                                                        bool daemon, bool inherit) {
    return std::make_unique<PosixProcess>(daemon, inherit);
};

std::unique_ptr<Process> Process::FromPid(Pid pid) {
    return std::make_unique<PosixProcess>(pid);
}

std::unique_ptr<Process> Process::Me() {
    return FromPid(getpid());
}

std::vector<std::unique_ptr<Process>> Process::FromName(const std::string& name) {
    std::vector<std::unique_ptr<Process>> processes;

#ifdef __APPLE__
    // Get list of all processes.
    const size_t pid_array_size_needed = proc_listallpids(nullptr, 0);
    if (pid_array_size_needed <= 0) {
        return processes;
    }

    std::vector<pid_t> pid_array(pid_array_size_needed * 4);
    const int pid_count = proc_listallpids(
            pid_array.data(), static_cast<int>(pid_array.size() * sizeof(pid_array[0])));
    if (pid_count <= 0) {
        return processes;
    }

    pid_array.resize(pid_count);

    // Ok, now we have an array of pids, and we just find the
    // one with the proper executable substring.
    for (const auto pid : pid_array) {
        char pname[PROC_PIDPATHINFO_MAXSIZE] = {0};
        proc_pidpath(pid, pname, sizeof(pname));
        if (strstr(pname, name.c_str())) {
            processes.push_back(Process::FromPid(pid));
        }
    }
#else
    for (const auto& entry : android::base::file::scan_dir("/proc", /*fullPath=*/true)) {
        int pid = 0;
        if (std::sscanf(entry.string().c_str(), "/proc/%d", &pid) == 1) {
            if (ReadProcLinux(pid).find(name) != std::string::npos) {
                processes.push_back(Process::FromPid(pid));
            }
        }
    }
#endif

    return processes;
}

}  // namespace android::base
