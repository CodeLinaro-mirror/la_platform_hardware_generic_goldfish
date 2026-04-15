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

#include "android/base/threads/thread_utils.h"

#include <string>

// clang-format off
#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>

#include "android/base/win32_unicode_string.h"
#else
#include <pthread.h>
#endif
// clang-format on

namespace android {
namespace base {

void ThreadUtils::SetCurrentThreadName(std::string_view name) {
    if (name.empty()) {
        return;
    }
    const std::string name_str(name);

#if defined(_WIN32)
    SetThreadDescription(GetCurrentThread(), android::base::Win32UnicodeString(name_str).c_str());
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name_str.c_str());
#elif defined(__APPLE__)
    pthread_setname_np(name_str.c_str());
#endif
}

std::string ThreadUtils::GetCurrentThreadName() {
#if defined(_WIN32)
    PWSTR data;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &data))) {
        std::string result = android::base::Win32UnicodeString(data).toString();
        LocalFree(data);
        return result;
    }
    return "";
#elif defined(__linux__) || defined(__APPLE__)
    char buf[64];
    if (pthread_getname_np(pthread_self(), buf, sizeof(buf)) == 0) {
        return std::string(buf);
    }
    return "";
#else
    return "";
#endif
}

}  // namespace base
}  // namespace android
