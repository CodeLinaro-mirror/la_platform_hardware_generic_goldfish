/* Copyright (C) 2011 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

/*
 * This is the source for a dummy crash program that starts the crash reporter
 * and then crashes due to an undefined symbol
 */
#include <stdio.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "android/base/bazel/bazel_info.h"
#include "android/crashreport/Breadcrumb.h"
#include "android/crashreport/CrashConsent.h"
#include "android/crashreport/crash-initializer.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#endif

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"

// Declare flags using ABSL_FLAG
ABSL_FLAG(std::string, dll, "", "Path to the DLL to load.");  // Default: empty string
ABSL_FLAG(bool, nocrash, false, "Disable crash handling.");   // Default: false
ABSL_FLAG(int, delay_ms, 0, "Delay in milliseconds.");        // Default: 0

using android::base::Bazel;

using consentProviderFunction = android::crashreport::CrashConsent* (*)(void);

void crashme(int arg, bool nocrash, int delay_ms) {
    if (delay_ms) {
        LOG(INFO) << "Delaying crash by " << delay_ms << " ms";
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    for (int i = 0; i < arg; i++) {
        if (!nocrash) {
            volatile int* volatile ptr = nullptr;
            *ptr = 1313;  // die

#ifndef _WIN32
            raise(SIGABRT);
#endif
            abort();
        }
    }
}

#ifdef _WIN32
typedef HMODULE HandleType;
#else
typedef void* HandleType;
#endif

consentProviderFunction getConsentProvider(HandleType lib) {
#ifdef _WIN32
    return reinterpret_cast<consentProviderFunction>(GetProcAddress(lib, "consentProvider"));
#else
    return reinterpret_cast<consentProviderFunction>(dlsym(lib, "consentProvider"));
#endif
}

bool load_consent_provider(const std::string& fromDll) {
    HandleType library;
#ifdef _WIN32
    library = LoadLibraryA(fromDll.c_str());
#else
    library = dlopen(fromDll.c_str(), RTLD_NOW);
#endif

    if (!library) {
        LOG(ERROR) << "Unable to load dll, not overriding initializer.";
        return false;
    }

    // Probe for function symbol.
    auto provider = getConsentProvider(library);
    android::crashreport::inject_consent_provider(provider());
    return true;
}

/* Main routine */
int main(int argc, char** argv) {
    absl::InitializeLog();
    absl::log_internal::EnableSymbolizeLogStackTrace(true);
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
    absl::SetVLogLevel("*", 1);

    // Parse flags
    absl::ParseCommandLine(argc, argv);

    // Access flag values using absl::GetFlag
    std::string dll = absl::GetFlag(FLAGS_dll);
    bool nocrash = absl::GetFlag(FLAGS_nocrash);
    int delay_ms = absl::GetFlag(FLAGS_delay_ms);

    if (!dll.empty()) {
        load_consent_provider(dll);
    }

    crashhandler_init(argc, argv);
    TCRUMB() << "We are just getting started";
    LOG(INFO) << "Ready for " << (nocrash ? "clean exit" : "crash") << " in " << delay_ms << " ms";
    crashme(argc, nocrash, delay_ms);
    LOG(INFO) << "test crasher done";
    return 0;
}
