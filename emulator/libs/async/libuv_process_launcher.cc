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

#include <memory>
#include <vector>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task.h>
#endif

#include "absl/log/log.h"

#include "goldfish/async/launch_config.h"
#include "goldfish/async/uv_to_absl.h"

namespace goldfish::async {

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

absl::StatusOr<UvProcessLauncher::ProcessHandle> UvProcessLauncher::Launch(
        const LaunchConfig& config, uv_exit_cb exit_cb) {
    uv_stdio_container_t stdio[3]{};
    if (config.keep_stdio) {
        stdio[0].flags = UV_IGNORE;  // The default.
        stdio[1].flags = UV_INHERIT_FD;
        stdio[1].data.fd = 1;
        stdio[2].flags = UV_INHERIT_FD;
        stdio[2].data.fd = config.daemon ? 1 : 2;  // Send daemon stderr debugging to stdout too.
    }

    const std::string exe = config.exe_path.string();

    VLOG(1) << "Launching " << exe << ": " << config.daemon;
    // Flush the streams to try to reduce interleaved logs from the different processes.
    std::cout << std::flush;
    std::cerr << std::flush;
    std::vector<char*> args;
    args.push_back(const_cast<char*>(exe.c_str()));
    for (const auto& arg : config.args) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

    const uv_process_options_t options{
        // const char* cwd;
        // TODO char** env;
        .exit_cb = exit_cb,  .file = exe.c_str(),
        .args = args.data(), .flags = config.daemon ? UV_PROCESS_DETACHED : 0U,
        .stdio_count = 3,    .stdio = stdio,
    };

    auto handle = ProcessHandle(new uv_process_t{});
    handle->data = this;

#ifdef __APPLE__
    // uv_spawn will automatically inherit our exception ports, which means that crashpad
    // will start tracking the child, which can result in some odd corner cases we want
    // to avoid like b/333628462
    // Note: we will not detect crashes until disable_ports leaves the scope.
    const ScopedDisableExceptionPorts disable_ports;
#endif
    if (const int res = uv_spawn(uv_loop_, handle.get(), &options); res < 0) {
        return goldfish::async::UvErrToAbslStatus(res);
    }

    if (config.daemon) {
        // Let launcher exit and leave daemon processes running.
        uv_unref(reinterpret_cast<uv_handle_t*>(handle.get()));
    }

    return handle;
}

}  // namespace goldfish::async
