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

// Define WINVER and _WIN32_WINNT to 0x0600 which matches Windows Vista.
// This is to get GetThreadUILanguage().
#ifdef WINVER
#undef WINVER
#endif
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600

// IWYU pragma: end_keep
// clang-format on
#include "android/base/win32_utils.h"

#include <windows.h>
#include <winnls.h>

// IWYU pragma: end_keep
// clang-format on
#include "gtest/gtest.h"

namespace android {
namespace base {

TEST(Win32Utils, getErrorString) {
    LANGID langid = ::GetThreadUILanguage();
    LANGID kLangIdEnglishUS = 1033;
    if (kLangIdEnglishUS != langid) {
        // This test only has US English strings
        fprintf(stderr,
                "Win32Utils::getErrorString skipping tests on non-US English "
                "system (LANGID %i)\n",
                langid);
        return;
    }

    std::string file_not_found = Win32Utils::getErrorString(2);
    EXPECT_TRUE(
            0 == strcmp("The system cannot find the file specified.\r\n", file_not_found.c_str()) ||
            0 == strcmp("File not found.\r\n", file_not_found.c_str()));
}

}  // namespace base
}  // namespace android
