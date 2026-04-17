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
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/process_launcher.h"
#include "uv.h"

namespace goldfish::async {

class UvProcessLauncher : public ProcessLauncher {
  private:
    struct ProcessHandleDeleter {
        void operator()(uv_process_t* handle) const {
            uv_close(reinterpret_cast<uv_handle_t*>(handle),
                     [](uv_handle_t* handle) { delete handle; });
        }
    };

  public:
    using ProcessHandle = std::unique_ptr<uv_process_t, ProcessHandleDeleter>;

    explicit UvProcessLauncher(LibuvEventLoop& event_loop);
    ~UvProcessLauncher() override;

    absl::StatusOr<std::unique_ptr<ManagedProcess>> Launch(const LaunchConfig& config,
                                                           ExitCallback exit_cb) override;

    void ForgetProcess(const ManagedProcess& process) override;

  private:
    class UvPipe;
    friend class UvPipe;
    void RemovePipedOutput(UvPipe* pipe);

    uv_loop_t* uv_loop_;
    std::vector<std::unique_ptr<UvPipe>> pipes_;
};

}  // namespace goldfish::async
