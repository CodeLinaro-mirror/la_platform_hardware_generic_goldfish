// Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <functional>
#include <memory>

#include "absl/status/statusor.h"

#include "goldfish/async/launch_config.h"

namespace goldfish::async {

class ManagedProcess {
  public:
    virtual ~ManagedProcess() = default;
    virtual int GetPid() const = 0;
    // Send signal to process. Return does not signify process received signal.
    virtual void Kill(int signum) = 0;
};

class ProcessLauncher {
  public:
    virtual ~ProcessLauncher() = default;

    using ExitCallback = std::function<void(int64_t exit_status, int term_signal)>;

    virtual absl::StatusOr<std::unique_ptr<ManagedProcess>> Launch(const LaunchConfig& config,
                                                                   ExitCallback exit_cb) = 0;

    // This prevents the parent loop from waiting for this process at shutdown.
    // Use on detached processes that should be able to keep running after the launcher
    // exits.
    virtual void ForgetProcess(const ManagedProcess& process) = 0;
};

}  // namespace goldfish::async
