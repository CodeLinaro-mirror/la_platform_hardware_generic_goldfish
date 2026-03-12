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
#if defined(UNICODE) || defined(_UNICODE)
#error We rely on UTF-8 codepage in the manifest
#endif

#include <windows.h>

#include <string_view>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"

#include "android/base/scoped_file_handle.h"
#include "android/base/win32_security.h"
#include "android/base/win32_utils.h"
#include "goldfish/file/file_atomic.h"

namespace android::base::file {

using android::base::Win32Security;
using android::base::Win32Utils;

namespace {

absl::Status WriteToHandle(HANDLE hFile, std::string_view content, const fs::path& path) {
    if (content.size() > static_cast<size_t>((std::numeric_limits<DWORD>::max)())) {
        return absl::InvalidArgumentError(
                absl::StrCat("Content size too large for Windows API: ", path.string(), "."));
    }

    DWORD written;
    if (!WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()), &written, NULL)) {
        return absl::InternalError(
                absl::StrCat("Failed to write data to '", path.string(),
                             "'. Error: ", Win32Utils::getErrorString(GetLastError())));
    }
    CHECK(written == content.size()) << "The windows API should guarantee all bytes are written.";

    if (!FlushFileBuffers(hFile)) {
        return absl::InternalError(
                absl::StrCat("Failed to flush buffers for '", path.string(),
                             "'. Error: ", Win32Utils::getErrorString(GetLastError())));
    }

    return absl::OkStatus();
}

}  // namespace

absl::Status CreatePrivateFileExclusive(const fs::path& path, std::string_view content) noexcept {
    SECURITY_DESCRIPTOR sd;
    auto paclRes = Win32Security::SetPrivateDacl(&sd);
    if (!paclRes.ok()) return paclRes.status();

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    android::base::ScopedFileHandle hFile(CreateFileA(path.string().c_str(), GENERIC_WRITE, 0, &sa,
                                                      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    if (!hFile.ok()) {
        DWORD err = GetLastError();
        const std::string path_str = path.string();
        if (err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS) {
            return absl::AlreadyExistsError(absl::StrCat(
                    "The file '", path_str,
                    "' already exists and cannot be overwritten for security reasons."));
        }
        if (err == ERROR_ACCESS_DENIED) {
            return absl::PermissionDeniedError(
                    absl::StrCat("Access denied when creating '", path_str,
                                 "'. Ensure you have write permissions for the directory."));
        }
        return absl::InternalError(absl::StrCat("Failed to create '", path_str,
                                                "'. Error: ", Win32Utils::getErrorString(err)));
    }

    auto status = WriteToHandle(hFile.get(), content, path);
    if (!status.ok()) {
        hFile.close();
        ::DeleteFileW(path.c_str());
        return status;
    }

    return absl::OkStatus();
}

}  // namespace android::base::file
