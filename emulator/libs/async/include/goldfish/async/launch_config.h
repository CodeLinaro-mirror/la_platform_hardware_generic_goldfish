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

#include <filesystem>
#include <vector>

namespace goldfish::async {

struct LaunchConfig {
    std::filesystem::path exe_path;

    std::vector<std::string> args;

    std::vector<std::string> environment;

    bool daemon;

    // If true, the child process is placed in a new process group.
    // This provides signal isolation, preventing terminal-generated signals (like SIGINT from
    // Ctrl+C) from being sent directly to the child process. It allows the parent process to
    // intercept the signal and manage the child's graceful shutdown (e.g., saving a snapshot before
    // exiting). In libuv, this translates to the UV_PROCESS_DETACHED flag.
    bool new_process_group = false;

    // Note that kInherit doesn't work on Windows with new_process_group=true.
    // kPipe defaults to std::cout and std::cerr.
    enum class StdioMode {
        kNone,     // stdio redirected to /dev/null.
        kInherit,  // Inherit stdio from the parent.
        kPipe,     // Pipe stdout+stderr back to the parent and write to ostream.
    };
    StdioMode stdio_mode = StdioMode::kNone;

    bool redirect_stderr_to_stdout = false;
    std::filesystem::path pipe_stdout_path;
    std::filesystem::path pipe_stderr_path;
};

}  // namespace goldfish::async
