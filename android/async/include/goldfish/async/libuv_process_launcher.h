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

#include "absl/status/statusor.h"

#include "goldfish/async/launch_config.h"
#include "uv.h"

namespace goldfish::async {

class UvProcessLauncher {
  protected:
    using ProcessHandle = std::unique_ptr<uv_process_t>;

    static UvProcessLauncher& get_launcher(const uv_process_t& handle) {
        return *static_cast<UvProcessLauncher*>(handle.data);
    }

    static void close_handle(ProcessHandle handle) {
        uv_close((uv_handle_t*)handle.get(), nullptr);
    }

    static int get_pid(const ProcessHandle& handle) { return handle->pid; }

    UvProcessLauncher(uv_loop_t* uv_loop) : mUvLoop(uv_loop) {}

    absl::StatusOr<ProcessHandle> launch(const LaunchConfig& config, uv_exit_cb exit_cb);

  private:
    uv_loop_t* mUvLoop;
};

}  // namespace goldfish::async