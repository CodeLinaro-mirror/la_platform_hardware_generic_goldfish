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

#pragma once

#ifndef _WIN32
// nothing's here for Posix
#else  // _WIN32

#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace android {
namespace base {

// This class holds various utility functions for Windows host systems.
// All methods here must be static!
class Win32Utils {
  public:
    // Creates a UTF-8 encoded error message string from a Windows System Error
    // Code.  String returned depends on current language id.  See
    // FormatMessage.
    static std::string getErrorString(DWORD error_code);

    // This function dynamically loads "Ntdll.dll" and calls RtlGetVersion in
    // order to properly identify the OS version. GetVersionEx will always
    // return 6.2 for Windows 8 and later versions (unless the binary is
    // manifested for a specific OS version).
    static std::optional<_OSVERSIONINFOEXW> getWindowsVersion();
};

}  // namespace base
}  // namespace android

#endif  // _WIN32
