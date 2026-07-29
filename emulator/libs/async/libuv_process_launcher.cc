// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "goldfish/async/libuv_process_launcher.h"

#include <sys/types.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "uv.h"

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task.h>
#endif

#include "absl/log/log.h"

#include "android/base/fd_util.h"
#include "goldfish/async/launch_config.h"
#include "goldfish/async/process_launcher.h"
#include "goldfish/async/uv_to_absl.h"

namespace goldfish::async {

namespace {
class UvManagedProcess : public ManagedProcess {
  public:
    UvManagedProcess(ProcessLauncher::ExitCallback exit_cb)
            : mHandle(new uv_process_t{}), mExitCb(std::move(exit_cb)) {
        mHandle->data = this;
    }

    int GetPid() const override { return mHandle->pid; }

    void Kill(int signum) override { uv_process_kill(mHandle.get(), signum); }

    void OnExit(int64_t exit_status, int term_signal) {
        if (mExitCb) {
            mExitCb(exit_status, term_signal);
        }
    }

    uv_process_t* handle() const { return mHandle.get(); }

  private:
    UvProcessLauncher::ProcessHandle mHandle;
    ProcessLauncher::ExitCallback mExitCb;
};

void uv_internal_exit_cb(uv_process_t* req, int64_t exit_status, int term_signal) {
    auto* process = static_cast<UvManagedProcess*>(req->data);
    process->OnExit(exit_status, term_signal);
}
}  // namespace

#ifdef __APPLE__
namespace {
class ScopedDisableExceptionPorts {
  public:
    ScopedDisableExceptionPorts() {
        // Load current state..
        const kern_return_t kr = task_get_exception_ports(mach_task_self(), EXC_MASK_ALL, masks_,
                                                          &count_, ports_, behaviors_, flavors_);
        if (kr == KERN_SUCCESS) {
            // Disable all exception ports, crashpad will not be able to detect if a crash happens
            // after this point.
            if (task_set_exception_ports(mach_task_self(), EXC_MASK_ALL, MACH_PORT_NULL, 0, 0) ==
                KERN_SUCCESS) {
                active_ = true;
            }
        }
    }

    ~ScopedDisableExceptionPorts() {
        if (active_) {
            // Reactivate the ports, crash handling is back!
            for (unsigned int i = 0; i < count_; ++i) {
                task_set_exception_ports(mach_task_self(), masks_[i], ports_[i], behaviors_[i],
                                         flavors_[i]);
            }
        }
    }

  private:
    bool active_ = false;
    static constexpr unsigned int kMaxExceptionPorts = 32;
    mach_msg_type_number_t count_ = kMaxExceptionPorts;
    exception_mask_t masks_[kMaxExceptionPorts];
    mach_port_t ports_[kMaxExceptionPorts];
    exception_behavior_t behaviors_[kMaxExceptionPorts];
    thread_state_flavor_t flavors_[kMaxExceptionPorts];
};
}  // namespace
#endif

class UvProcessLauncher::UvPipe {
  public:
    UvPipe(UvProcessLauncher* l, std::ostream& std_os, const std::filesystem::path& path)
            : launcher_(l), read_buffer_(65535), output_(path.empty() ? std_os : output_file_) {
        if (const int err = uv_pipe_init(l->uv_loop_, &pipe_, /*ipc=*/0)) {
            LOG(DFATAL) << "uv_pipe_init failed with: " << uv_strerror(err);
        }
        pipe_.data = this;

        if (!path.empty()) {
            output_file_.open(path, std::ios::binary | std::ios::app);
        }
    }

    ~UvPipe() {
        output_file_.close();
        uv_close(reinterpret_cast<uv_handle_t*>(&pipe_), [](uv_handle_t* handle) {
            // Nothing to do.
        });
    }

    bool StartReading() {
        if (const int err =
                    uv_read_start(reinterpret_cast<uv_stream_t*>(&pipe_), OnAlloc, OnRead)) {
            LOG(DFATAL) << "uv_read_start failed with: " << uv_strerror(err);
            return false;
        }
        return true;
    }

    uv_pipe_t& GetPipe() { return pipe_; }

    static std::unique_ptr<UvPipe> Create(UvProcessLauncher* launcher, std::ostream& std_os,
                                          const std::filesystem::path& path) {
        return std::make_unique<UvPipe>(launcher, std_os, path);
    }

  private:
    static void OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
        auto* uv_pipe = static_cast<UvPipe*>(handle->data);
        buf->base = uv_pipe->read_buffer_.data();
        buf->len = uv_pipe->read_buffer_.size();
    }

    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
        auto* uv_pipe = static_cast<UvPipe*>(stream->data);
        if (nread > 0) {
            uv_pipe->output_.write(buf->base, nread);
        } else if (nread < 0) {
            VLOG(1) << "UvPipe reading finished";
            uv_pipe->launcher_->RemovePipedOutput(uv_pipe);
        }
    }

    UvProcessLauncher* launcher_;
    std::vector<char> read_buffer_;

    std::ofstream output_file_;
    std::ostream& output_;

    uv_pipe_t pipe_;
};

UvProcessLauncher::UvProcessLauncher(LibuvEventLoop& event_loop)
        : uv_loop_(static_cast<uv_loop_t*>(event_loop.GetRawLoop())) {}
UvProcessLauncher::~UvProcessLauncher() {}

void UvProcessLauncher::RemovePipedOutput(UvPipe* pipe) {
    std::erase_if(pipes_, [pipe](const auto& p) { return p.get() == pipe; });
}

absl::StatusOr<std::unique_ptr<ManagedProcess>> UvProcessLauncher::Launch(
        const LaunchConfig& config, ExitCallback exit_cb) {
    // Try not to pass any FDs to children.
    // Note that this can race with other threads creating FDs (without CLOEXEC).
    android::base::SetAllFdsCloexec();

    std::vector<std::unique_ptr<UvPipe>> new_pipes;

    LaunchConfig::StdioMode stdio_mode = config.stdio_mode;
#ifdef _WIN32
    // On Windows, processes in a new process group can't inherit access to the terminal as they are
    // detached.
    if (config.new_process_group && stdio_mode == LaunchConfig::StdioMode::kInherit) {
        stdio_mode = LaunchConfig::StdioMode::kPipe;
    }
#endif

    uv_stdio_container_t stdio[3]{};
    // By default (UV_IGNORE), stdio will be connected to /dev/null.
    switch (stdio_mode) {
        using enum LaunchConfig::StdioMode;
    case kInherit: {
        stdio[0].flags = UV_IGNORE;  // The default.
        stdio[1].flags = UV_INHERIT_FD;
        stdio[1].data.fd = 1;
        stdio[2].flags = UV_INHERIT_FD;
        stdio[2].data.fd = config.redirect_stderr_to_stdout ? 1 : 2;
    } break;
    case kPipe: {
        new_pipes.push_back(UvPipe::Create(this, std::cout, config.pipe_stdout_path));
        stdio[1].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        stdio[1].data.stream = reinterpret_cast<uv_stream_t*>(&new_pipes.back()->GetPipe());

        new_pipes.push_back(
                UvPipe::Create(this, config.redirect_stderr_to_stdout ? std::cout : std::cerr,
                               config.redirect_stderr_to_stdout ? config.pipe_stdout_path
                                                                : config.pipe_stderr_path));
        stdio[2].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        stdio[2].data.stream = reinterpret_cast<uv_stream_t*>(&new_pipes.back()->GetPipe());
    } break;
    case kNone:
    default:
        break;
    }

    const std::string exe = config.exe_path.string();

    VLOG(1) << "Launching " << exe;
    // Flush the streams to try to reduce interleaved logs from the different processes.
    std::cout << std::flush;
    std::cerr << std::flush;
    std::vector<char*> args;
    args.push_back(const_cast<char*>(exe.c_str()));
    for (const auto& arg : config.args) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

    unsigned int flags = 0;
    if (config.daemon || config.new_process_group) {
        flags |= UV_PROCESS_DETACHED;
    }

    const uv_process_options_t options{
        // const char* cwd;
        // TODO char** env;
        .exit_cb = uv_internal_exit_cb,
        .file = exe.c_str(),
        .args = args.data(),
        .flags = flags,
        .stdio_count = 3,
        .stdio = stdio,
    };

    auto managed_process = std::make_unique<UvManagedProcess>(std::move(exit_cb));

#ifdef __APPLE__
    // uv_spawn will automatically inherit our exception ports, which means that crashpad
    // will start tracking the child, which can result in some odd corner cases we want
    // to avoid like b/333628462
    // Note: we will not detect crashes until disable_ports leaves the scope.
    const ScopedDisableExceptionPorts disable_ports;
#endif
    if (const int res = uv_spawn(uv_loop_, managed_process->handle(), &options); res < 0) {
        return goldfish::async::UvErrToAbslStatus(res);
    }

    // Start reading from new pipes and move them to member vector.
    for (auto& pipe : new_pipes) {
        if (pipe->StartReading()) {
            pipes_.push_back(std::move(pipe));
        }
    }

    return managed_process;
}

void UvProcessLauncher::ForgetProcess(const ManagedProcess& process) {
    // Let launcher exit and leave daemon processes running.
    const auto& uv_process = static_cast<const UvManagedProcess&>(process);
    uv_unref(reinterpret_cast<uv_handle_t*>(uv_process.handle()));
}

}  // namespace goldfish::async
