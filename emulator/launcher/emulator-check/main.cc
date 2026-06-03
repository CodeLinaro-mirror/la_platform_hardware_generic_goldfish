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
// limitations under the License.

/**
 * Emulator Check Tool
 *
 * This tool performs various system checks to verify host capabilities and
 * environment details, primarily for the Android Emulator.
 *
 * Command Line Interface:
 *
 * Usage: emulator-check <argument1> [<argument2> ...]
 *
 * Arguments:
 *   -h, -help, --help
 *          Show the help message and exit immediately. Prints raw usage text.
 *
 *   accel
 *          Check CPU acceleration support (e.g., KVM on Linux, HVF on Mac, WHPX/AEHD on Windows).
 *          Output Code: AndroidCpuAcceleration enum value (0 for READY, others for various errors).
 *
 *   hyper-v
 *          Check if Hyper-V is installed and running (primarily Windows).
 *          Output Code: AndroidHyperVStatus enum value (0: ABSENT, 1: INSTALLED, 2: RUNNING, 100:
 * ERROR).
 *
 *   cpu-info
 *          Return CPU model and capabilities.
 *          Output Code: AndroidCpuInfoFlags bitmask.
 *          Message: Pipe-separated (|) list of CPU features/details (e.g., "Intel
 * CPU|Virtualization is supported|Inside a VM|64-bit CPU|").
 *
 *   window-mgr
 *          Return the current window manager name (Linux only, returns platform name on
 * Mac/Windows). Output Code: 0 on success, 100 on error.
 *
 *   desktop-env
 *          Return the current desktop environment name (Linux only, returns platform name on
 * Mac/Windows). Output Code: 0 on success, 100 on error.
 *
 *   whpx (Windows only)
 *          Check if Windows Hypervisor Platform (WHPX) is installed and running.
 *          Output Code: 0 on success, HRESULT on error.
 *
 *   enable-whpx (Windows only)
 *          Enable Windows Hypervisor Platform. Requires admin privileges.
 *          Output Code: 0 on success, HRESULT on error.
 *
 *   disable-whpx (Windows only)
 *          Disable Windows Hypervisor Platform. Requires admin privileges.
 *          Output Code: 0 on success, HRESULT on error.
 *
 * Output Format (for standard checks):
 *   For each standard argument, the tool outputs exactly 4 lines to stdout:
 *   Line 1: <argument>:
 *   Line 2: <return_code_integer>
 *   Line 3: <single_line_text_message>
 *   Line 4: <argument>
 *
 *   Example:
 *     accel:
 *     0
 *     KVM (version 12) is installed and usable.
 *     accel
 *
 *   If an argument is unknown:
 *     <argument>:
 *     100
 *     Unknown argument
 *     <argument>
 *
 * Exit Code:
 *   The exit code of the program is the return code of the FIRST executed check.
 *   If no arguments are provided, or if there is an initialization error, it exits with 100.
 */

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

#include "platform_info.h"
#include "android/cpu/cpu_accelerator.h"

using CommandReturn = std::pair<int, std::string>;

static const int kGenericError = 100;

static CommandReturn help();

// check ability to launch kvm accelerated VM and exit
// designed for use by Android Studio
static CommandReturn checkCpuAcceleration() {
    return std::make_pair(android::GetCurrentCpuAcceleratorStatusCode(),
                          android::GetCurrentCpuAcceleratorStatus());
}

static CommandReturn checkHyperV() {
    auto status = android::GetHyperVStatus();
    return std::make_pair(status.first, status.second);
}

static CommandReturn getCpuInfo() {
    auto pair = android::GetCpuInfo();
    // GetCpuInfo() returns a set of lines, we need to turn it into a
    // single line.
    std::replace(pair.second.begin(), pair.second.end(), '\n', '|');
    return std::make_pair(pair.first, pair.second);
}

static CommandReturn getWindowManager() {
    const std::string name = android::getWindowManagerName();
    return std::make_pair(name.empty() ? kGenericError : 0, name);
}

static CommandReturn getDesktopEnv() {
    const std::string name = android::getDesktopEnvironmentName();
    return std::make_pair(name.empty() ? kGenericError : 0, name);
}

static CommandReturn unimplemented() {
    return std::make_pair(kGenericError, "unimplemented");
}

constexpr struct Option {
    const char* arg;
    const char* help;
    CommandReturn (*handler)();
    bool printRawAndStop;
} options[] = {
    {"-h", "\tShow this help message", &help, true},
    {"-help", "Show this help message", &help, true},
    {"--help", "Show this help message", &help, true},
    {"accel", "Check the CPU acceleration support", &checkCpuAcceleration},
    {"hyper-v", "Check if hyper-v is installed and running (Windows)", &checkHyperV},
    {"cpu-info", "Return the CPU model information", &getCpuInfo},
    {"window-mgr", "Return the current window manager name", &getWindowManager},
    {"desktop-env", "Return the current desktop environment name", &getDesktopEnv},
#ifdef _WIN32
    {"whpx", "Check if WHPX is installed and running (Windows)", &unimplemented},
    {"enable-whpx", "Enable Windows Hypervisor Platform in Windows Features (Windows)",
     &unimplemented},
    {"disable-whpx", "Disable Windows Hypervisor Platform in Windows Features (Windows)",
     &unimplemented},
#endif
};

static std::string usage() {
    std::ostringstream str;
    str <<
            R"(Usage: emulator-check <argument1> [<argument2>...]

Performs the set of checks requested in <argumentX> and returns the result in
the following format:
<argument1>:
<return code for <argument1>>
<a single line of text information returned for <argument1>>
<argument1>
<argument2>:
<return code for <argument2>>
<a single line of text information returned for <argument2>>
<argument2>
...

<argumentX> is any of:

)";

    for (const auto& option : options) {
        str << "    " << option.arg << "\t\t" << option.help << '\n';
    }

    str << '\n';

    return str.str();
}

static CommandReturn help() {
    return std::make_pair(0, usage());
}

static int error(const char* format, const char* arg = nullptr) {
    if (format) {
        fprintf(stderr, format, arg);
        fprintf(stderr, "\n\n");
    }
    fprintf(stderr, "%s\n", usage().c_str());
    return kGenericError;
}

static int processArguments(int argc, const char* const* argv) {
    std::optional<int> retCode;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto opt = std::find_if(std::begin(options), std::end(options),
                                      [arg](const Option& opt) { return arg == opt.arg; });
        if (opt == std::end(options)) {
            printf("%s:\n%d\nUnknown argument\n%s\n", arg.data(), kGenericError, arg.data());
            continue;
        }

        auto handlerRes = opt->handler();
        if (!retCode) {
            // Remember the first return value for the compatibility with the
            // previous version.
            retCode = handlerRes.first;
        }

        if (opt->printRawAndStop) {
            printf("%s\n", handlerRes.second.c_str());
            break;
        }

        printf("%s:\n%d\n%s\n%s\n", arg.data(), handlerRes.first, handlerRes.second.c_str(),
               arg.data());
    }

    return retCode.value_or(kGenericError);
}

int main(int argc, const char* const* argv) {
    if (argc < 2) {
        return error("Missing a required argument(s)");
    }

    return processArguments(argc, argv);
}
