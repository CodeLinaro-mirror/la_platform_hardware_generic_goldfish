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

#pragma once

#include <memory>
#include <vector>

#include "absl/status/statusor.h"

#include "goldfish/async/launch_config.h"
#include "uv.h"

namespace goldfish::async {

class UvProcessLauncher {
  private:
    struct ProcessHandleDeleter {
        void operator()(uv_process_t* handle) const {
            uv_close(reinterpret_cast<uv_handle_t*>(handle),
                     [](uv_handle_t* handle) { delete handle; });
        }
    };

  public:
    class UvPipe;

  protected:
    using ProcessHandle = std::unique_ptr<uv_process_t, ProcessHandleDeleter>;

    static UvProcessLauncher& GetLauncher(const uv_process_t& handle) {
        return *static_cast<UvProcessLauncher*>(handle.data);
    }

    static int GetPid(const ProcessHandle& handle) { return handle->pid; }

    explicit UvProcessLauncher(uv_loop_t* uv_loop);
    ~UvProcessLauncher();

    absl::StatusOr<ProcessHandle> Launch(const LaunchConfig& config, uv_exit_cb exit_cb);

    // This prevents the parent loop from waiting for this process at shutdown.
    // Use on detached processes that should be able to keep running after the launcher
    // exits.
    static void ForgetUvProcess(const ProcessHandle& handle);

  private:
    friend class UvPipe;
    void RemovePipedOutput(UvPipe* pipe);

    uv_loop_t* uv_loop_;
    std::vector<std::unique_ptr<UvPipe>> pipes_;
};

}  // namespace goldfish::async
