// Copyright 2020 The Android Open Source Project
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
#include <shlwapi.h>
#include <windows.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

#include "android/base/win32_unicode_string.h"
#include "goldfish/memory/shared_memory.h"

namespace goldfish::memory {

using namespace std::string_view_literals;

SharedMemory::SharedMemory(std::string_view path_or_uri, size_t size, DestructionPolicy policy)
        : size_(size), destruction_policy_(policy) {
    static constexpr std::string_view kFileUri = "file://"sv;
    const android::base::Win32UnicodeString srcUri(std::string(path_or_uri).c_str());
    if (path_or_uri.starts_with(kFileUri)) {
        WCHAR path[MAX_PATH];
        DWORD cPath = MAX_PATH;
        HRESULT hr = PathCreateFromUrlW(srcUri.c_str(), path, &cPath, NULL);
        CHECK(hr == S_OK) << "Failed to extract uri from: " << path_or_uri << " hr:" << hr;
        backing_file_ = std::filesystem::path(path).lexically_normal().string();
    } else {
        backing_file_ = std::filesystem::path(srcUri.c_str()).lexically_normal().string();
    }
}

absl::Status SharedMemory::Create(std::filesystem::perms mode) {
    return OpenInternal(AccessMode::kReadWrite, true, true);
}

absl::Status SharedMemory::CreateNoMapping(std::filesystem::perms mode) {
    return OpenInternal(AccessMode::kReadWrite, true, false);
}

absl::Status SharedMemory::Open(AccessMode access) {
    return OpenInternal(access, false, true);
}

absl::Status SharedMemory::OpenInternal(AccessMode access, bool create, bool do_mapping) {
    if (IsOpen()) {
        return absl::AlreadyExistsError("Shared memory already open");
    }
    create_ = create;

    const android::base::Win32UnicodeString wide_name(backing_file_);
    DWORD pageAccess = (access == AccessMode::kReadWrite) ? PAGE_READWRITE : PAGE_READONLY;

    HANDLE hFile;
    if (create) {
        hFile = CreateFileW(wide_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) {
                return absl::AlreadyExistsError("Shared memory file already exists.");
            }
            return absl::InternalError("Failed to create file for shared memory: " +
                                       std::to_string(GetLastError()));
        }
    } else {
        hFile = CreateFileW(
                wide_name.c_str(),
                (access == AccessMode::kReadWrite) ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            return absl::NotFoundError("Failed to open file for shared memory: " +
                                       std::to_string(GetLastError()));
        }
    }
    file_ = hFile;
    fd_ = CreateFileMappingW(file_, NULL, pageAccess, 0, (DWORD)size_, NULL);
    if (fd_ == NULL) {
        auto error = GetLastError();
        Close();
        return absl::InternalError("Failed to create/open shared memory region: " +
                                   std::to_string(error));
    }

    if (do_mapping) {
        DWORD desiredAccessView =
                (access == AccessMode::kReadWrite) ? FILE_MAP_WRITE : FILE_MAP_READ;
        address_ = MapViewOfFile(fd_, desiredAccessView, 0, 0, size_);
        if (address_ == NULL) {
            auto error = GetLastError();
            Close();
            return absl::InternalError("Failed to MapViewOfFile: " + std::to_string(error));
        }
    }

    // The file size holds the source of truth..
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(hFile, &size)) {
        Close();
        return absl::InternalError("Failed to retrieve file size for shared memory: " +
                                   std::to_string(GetLastError()));
    }
    VLOG(1) << "Explicitly set size from " << size_ << " bytes" << " to " << size.QuadPart
            << " bytes";
    size_ = static_cast<size_t>(size.QuadPart);
    return absl::OkStatus();
}

void SharedMemory::Close() {
    if (address_ != nullptr) {
        UnmapViewOfFile(address_);
        address_ = nullptr;
    }
    if (fd_ != kInvalidHandle) {
        CloseHandle(fd_);
        fd_ = kInvalidHandle;
    }
    if (file_ != kInvalidHandle) {
        CloseHandle(file_);
        file_ = kInvalidHandle;

        bool should_unlink = (create_ && destruction_policy_ == DestructionPolicy::kAuto) ||
                             (destruction_policy_ == DestructionPolicy::kDestroy);

        if (destruction_policy_ == DestructionPolicy::kKeep) {
            should_unlink = false;
        }

        if (should_unlink) {
            // Best effort, if another file has the handle this will fail.
            const android::base::Win32UnicodeString name(backing_file_);
            DeleteFileW(name.c_str());
        }
    }

    CHECK(!IsOpen()) << "Explicitly closing of " << backing_file_ << " failed.";
}

bool SharedMemory::IsOpen() const {
    return fd_ != kInvalidHandle;
}

}  // namespace goldfish::memory
