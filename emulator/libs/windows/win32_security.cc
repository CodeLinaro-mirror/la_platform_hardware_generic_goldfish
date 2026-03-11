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

#include <aclapi.h>

#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/base/scoped_file_handle.h"
#include "android/base/win32_utils.h"

namespace android::base {

namespace {

std::string FormatWin32Error(std::string_view message, DWORD error_code) {
    return absl::StrFormat("%s (0x%08X: %s)", message, error_code,
                           Win32Utils::getErrorString(error_code));
}

}  // namespace

// static
absl::StatusOr<std::vector<uint8_t>> Win32Security::GetCurrentUserSid() {
    HANDLE hTokenRaw;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hTokenRaw)) {
        return absl::InternalError(
                FormatWin32Error("Failed to open process token", GetLastError()));
    }
    ScopedFileHandle hToken(hTokenRaw);

    DWORD dwSize = 0;
    if (!GetTokenInformation(hToken.get(), TokenUser, nullptr, 0, &dwSize) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return absl::InternalError(
                FormatWin32Error("Failed to query token information size", GetLastError()));
    }
    std::vector<uint8_t> buffer(dwSize);
    if (!GetTokenInformation(hToken.get(), TokenUser, buffer.data(), dwSize, &dwSize)) {
        return absl::InternalError(
                FormatWin32Error("Failed to get token information", GetLastError()));
    }

    PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(buffer.data());
    DWORD sidLen = GetLengthSid(pTokenUser->User.Sid);
    std::vector<uint8_t> sid(sidLen);
    if (!CopySid(sidLen, sid.data(), pTokenUser->User.Sid)) {
        return absl::InternalError(FormatWin32Error("Failed to copy user SID", GetLastError()));
    }
    return sid;
}

// static
absl::StatusOr<std::vector<uint8_t>> Win32Security::GetLocalSystemSid() {
    std::vector<uint8_t> systemSid(SECURITY_MAX_SID_SIZE);
    DWORD systemSidSize = static_cast<DWORD>(systemSid.size());
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid.data(), &systemSidSize)) {
        return absl::InternalError(
                FormatWin32Error("Failed to create LocalSystem SID", GetLastError()));
    }
    systemSid.resize(systemSidSize);
    return systemSid;
}

// static
absl::StatusOr<ScopedLocalMem> Win32Security::SetPrivateDacl(PSECURITY_DESCRIPTOR pSd) {
    if (!InitializeSecurityDescriptor(pSd, SECURITY_DESCRIPTOR_REVISION)) {
        return absl::InternalError(
                FormatWin32Error("Failed to initialize Security Descriptor", GetLastError()));
    }

    auto userSidRes = GetCurrentUserSid();
    if (!userSidRes.ok()) return userSidRes.status();

    auto systemSidRes = GetLocalSystemSid();
    if (!systemSidRes.ok()) return systemSidRes.status();

    // Define access control entries for User and System.
    EXPLICIT_ACCESSW ea[2] = {0};
    ea[0].grfAccessPermissions = GENERIC_ALL;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = NO_INHERITANCE;
    BuildTrusteeWithSidW(&ea[0].Trustee, reinterpret_cast<PSID>(userSidRes->data()));

    ea[1].grfAccessPermissions = GENERIC_ALL;
    ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance = NO_INHERITANCE;
    BuildTrusteeWithSidW(&ea[1].Trustee, reinterpret_cast<PSID>(systemSidRes->data()));

    // Create the DACL.
    PACL pAclRaw = nullptr;
    DWORD aclRes = SetEntriesInAclW(2, ea, nullptr, &pAclRaw);
    if (aclRes != ERROR_SUCCESS) {
        return absl::InternalError(
                FormatWin32Error("Failed to create Access Control List", aclRes));
    }
    ScopedLocalMem pAcl(pAclRaw);

    // Attach the DACL and protect from inheritance.
    if (!SetSecurityDescriptorDacl(pSd, TRUE, static_cast<PACL>(pAcl.get()), FALSE)) {
        return absl::InternalError(
                FormatWin32Error("Failed to set DACL on Security Descriptor", GetLastError()));
    }
    if (!SetSecurityDescriptorControl(pSd, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
        return absl::InternalError(
                FormatWin32Error("Failed to protect DACL from inheritance", GetLastError()));
    }

    return pAcl;
}

}  // namespace android::base
