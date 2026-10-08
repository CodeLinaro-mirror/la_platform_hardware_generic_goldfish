// Copyright (C) 2026 The Android Open Source Project
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

#include <CoreServices/CoreServices.h>

#include <functional>
#include <memory>

#include "absl/time/time.h"

#include "android/base/file_system_watcher.h"

namespace android::base {

/**
 * Configuration and OS-call injection hooks for the macOS FSEvents-based
 * FileSystemWatcher implementation.
 *
 * Production callers use default-constructed options (30s startup timeout and
 * system CoreServices calls). Unit tests can override individual hooks or
 * timeouts via CreateDarwinFileSystemWatcherForTesting() to deterministically
 * exercise fseventsd latency spikes, stream creation/start failures, and
 * run-loop races.
 */
struct DarwinWatcherOptions {
    using StreamCreateFn = std::function<FSEventStreamRef(
            CFAllocatorRef, FSEventStreamCallback, FSEventStreamContext*, CFArrayRef,
            FSEventStreamEventId, CFTimeInterval, FSEventStreamCreateFlags)>;
    using StreamStartFn = std::function<Boolean(FSEventStreamRef)>;
    using RunLoopSourceCreateFn =
            std::function<CFRunLoopSourceRef(CFAllocatorRef, CFIndex, CFRunLoopSourceContext*)>;

    absl::Duration startup_timeout = absl::Seconds(30);
    StreamCreateFn stream_create = FSEventStreamCreate;
    StreamStartFn stream_start = FSEventStreamStart;
    RunLoopSourceCreateFn run_loop_source_create = CFRunLoopSourceCreate;
};

/**
 * Creates a macOS FileSystemWatcher instance with custom DarwinWatcherOptions
 * for deterministic fault and timeout injection in unit tests.
 *
 * @param path Directory to watch for filesystem changes.
 * @param on_change_callback Callback invoked when filesystem events occur.
 * @param options Custom timeout and CoreServices API hooks.
 * @return A FileSystemWatcher instance, or nullptr if path is not a directory.
 */
std::unique_ptr<FileSystemWatcher> CreateDarwinFileSystemWatcherForTesting(
        const std::filesystem::path& path,
        FileSystemWatcher::FileSystemWatcherCallback on_change_callback,
        DarwinWatcherOptions options);

}  // namespace android::base
