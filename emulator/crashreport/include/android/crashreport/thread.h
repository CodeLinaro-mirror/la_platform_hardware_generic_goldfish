// Copyright 2026 The Android Open Source Project
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

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace android::crashreport {

/**
 * @brief Helper to obtain the OS-level thread ID that matches Crashpad's reporting.
 *
 * These are the droids you are looking for if you want to correlate threads in your code
 * with threads in Crashpad minidumps.
 *
 * On Linux, this returns the kernel thread ID (TID) via gettid().
 * On Windows, this returns the OS thread ID via GetCurrentThreadId().
 * On MacOS, this returns the 64-bit unique thread ID via pthread_threadid_np().
 */
inline uint64_t GetOsThreadId() {
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(__APPLE__)
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return static_cast<uint64_t>(syscall(SYS_gettid));
#endif
}

}  // namespace android::crashreport
