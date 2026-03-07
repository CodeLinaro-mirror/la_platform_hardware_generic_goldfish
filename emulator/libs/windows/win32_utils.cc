// Copyright (C) 2015 The Android Open Source Project
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

#include "android/base/win32_utils.h"

#include <string_view>

#include "absl/strings/str_format.h"

#include "android/base/win32_unicode_string.h"

namespace android {
namespace base {

// static
std::string Win32Utils::getErrorString(DWORD error_code) {
    std::string result;

    LPWSTR error_string = nullptr;

    DWORD format_result =
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr,
                           error_code, 0, (LPWSTR)&error_string, 2, nullptr);
    if (error_string) {
        result = Win32UnicodeString::convertToUtf8(error_string);
        ::LocalFree(error_string);
    } else {
        absl::StrAppendFormat(&result, "Error Code: %li (FormatMessage result: %li)", error_code,
                              format_result);
    }
    return result;
}

// static
std::optional<_OSVERSIONINFOEXW> Win32Utils::getWindowsVersion() {
    HMODULE hLib;
    OSVERSIONINFOEXW ver;
    ver.dwOSVersionInfoSize = sizeof(ver);

    typedef DWORD(WINAPI * RtlGetVersion_t)(OSVERSIONINFOEXW*);

    hLib = LoadLibraryW(L"Ntdll.dll");
    if (!hLib) {
        return std::nullopt;
    }

    auto f_RtlGetVersion = (RtlGetVersion_t)GetProcAddress(hLib, "RtlGetVersion");
    if (!f_RtlGetVersion || f_RtlGetVersion(&ver)) {
        // RtlGetVersion returns non-zero values in case of errors.
        ver = {};
    }

    FreeLibrary(hLib);
    return ver;
}

}  // namespace base
}  // namespace android
