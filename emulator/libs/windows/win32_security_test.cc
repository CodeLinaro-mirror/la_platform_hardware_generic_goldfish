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
#include "android/base/win32_security.h"

#include <sddl.h>
#include <windows.h>

#include "gtest/gtest.h"

namespace android {
namespace base {

TEST(Win32Security, GetCurrentUserSid) {
    auto res = Win32Security::GetCurrentUserSid();
    ASSERT_TRUE(res.ok()) << res.status().message();
    EXPECT_FALSE(res->empty());
    EXPECT_TRUE(IsValidSid(res->data()));
}

TEST(Win32Security, GetLocalSystemSid) {
    auto res = Win32Security::GetLocalSystemSid();
    ASSERT_TRUE(res.ok()) << res.status().message();
    EXPECT_FALSE(res->empty());
    EXPECT_TRUE(IsValidSid(res->data()));

    // Verify it is actually the System SID (S-1-5-18)
    LPWSTR szSid = nullptr;
    ASSERT_TRUE(ConvertSidToStringSidW(res->data(), &szSid));
    EXPECT_STREQ(L"S-1-5-18", szSid);
    LocalFree(szSid);
}

TEST(Win32Security, SetPrivateDacl) {
    SECURITY_DESCRIPTOR sd;
    auto res = Win32Security::SetPrivateDacl(&sd);
    ASSERT_TRUE(res.ok()) << res.status().message();

    // Verify SD has a DACL.
    BOOL daclPresent = FALSE;
    BOOL daclDefaulted = FALSE;
    PACL pDacl = nullptr;
    ASSERT_TRUE(GetSecurityDescriptorDacl(&sd, &daclPresent, &pDacl, &daclDefaulted));
    EXPECT_TRUE(daclPresent);
    EXPECT_NE(nullptr, pDacl);

    // Verify SD is protected from inheritance.
    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD revision;
    ASSERT_TRUE(GetSecurityDescriptorControl(&sd, &control, &revision));
    EXPECT_TRUE(control & SE_DACL_PROTECTED);
}

}  // namespace base
}  // namespace android
