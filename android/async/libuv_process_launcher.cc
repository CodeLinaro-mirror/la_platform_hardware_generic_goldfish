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

#include "absl/log/log.h"

#include "goldfish/async/launch_config.h"
#include "goldfish/async/uv_to_absl.h"

namespace goldfish::async {

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
    char* args[config.args.size() + 2];
    args[0] = const_cast<char*>(exe.c_str());
    for (size_t i = 0; i < config.args.size(); ++i) {
        args[i + 1] = const_cast<char*>(config.args[i].c_str());
    }
    args[config.args.size() + 1] = nullptr;

    const uv_process_options_t options{
        // const char* cwd;
        // TODO char** env;
        .exit_cb = exit_cb, .file = exe.c_str(),
        .args = args,       .flags = config.daemon ? UV_PROCESS_DETACHED : 0U,
        .stdio_count = 3,   .stdio = stdio,
    };

    auto handle = std::make_unique<uv_process_t>();
    handle->data = this;
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
