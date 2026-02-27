// Copyright 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "debug.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#endif

#include "goldfish/base/array_size.h"

namespace android::base {

namespace {
#ifdef __linux__
std::string ReadFile(const std::filesystem::path& path) {
    const std::ifstream is(path);

    if (!is) {
        return {};
    }

    std::ostringstream ss;
    ss << is.rdbuf();
    return ss.str();
}
#endif
} // namespace

bool IsDebuggerAttached() {
#ifdef _WIN32
    return ::IsDebuggerPresent() != 0;
#elif defined(__linux__)
    const std::string proc_status = ReadFile("/proc/self/status");

    static constexpr std::string_view kTracerPidPrefix = "TracerPid:";
    const auto tracer_pid = proc_status.find(kTracerPidPrefix.data(), 0, kTracerPidPrefix.size());
    if (tracer_pid == std::string::npos) {
        return false;
    }

    // If the tracer PID is parseable and not 0, there's a debugger attached.
    const bool debugger_attached =
        atoi(proc_status.c_str() + tracer_pid + kTracerPidPrefix.size()) != 0;
    return debugger_attached;
#elif defined(__APPLE__)
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct kinfo_proc proc_info = {};
    size_t info_size = sizeof(proc_info);
    const int res = sysctl(mib, ARRAY_SIZE(mib), &proc_info, &info_size, nullptr, 0);
    if (res) {
        return false;
    }
    return (proc_info.kp_proc.p_flag & P_TRACED) != 0;
#else
#error Unsupported platform
#endif
}

bool WaitForDebugger(int64_t timeout_ms) {
    static const int64_t kSleepTimeoutMs = 500;

    int64_t slept_for_ms = 0;
    while (!IsDebuggerAttached() && (timeout_ms == -1 || slept_for_ms < timeout_ms)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kSleepTimeoutMs));
        slept_for_ms += kSleepTimeoutMs;
    }
    return IsDebuggerAttached();
}

void DebugBreak() {
#ifdef _WIN32
    ::DebugBreak();
#else
#ifdef __x86_64__
    asm("int $3");
#elif defined(__aarch64)
    asm("bkpt");
#endif
#endif
}

} // namespace android::base
