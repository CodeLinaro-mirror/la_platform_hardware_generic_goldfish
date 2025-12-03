// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"

#include "aemu/base/EintrWrapper.h"
#include "android/base/system/File.h"
#include "android/base/system/storage_capacity.h"

namespace android::base::file {

bool exists(const fs::path& path) {
    std::error_code ec;
    // Ignore EC - false is returned when there's an error too.
    return fs::exists(path, ec);
    // Alternatively:
    // int ret = path_access(path, F_OK);
    // return (ret == 0) || (errno != ENOENT);
}

bool is_file(const fs::path& path) {
    std::error_code ec;
    // Ignore EC - false is returned when there's an error too.
    return fs::is_regular_file(path, ec);
}

bool is_dir(const fs::path& path) {
    std::error_code ec;
    // Ignore EC - false is returned when there's an error too.
    return fs::is_directory(path, ec);
}

bool is_link(const fs::path& path) {
    std::error_code ec;
    // Ignore EC - false is returned when there's an error too.
    return fs::is_symlink(path, ec);
}

namespace {

#ifdef _WIN32
static int GetWin32Mode(int mode) {
    // Convert |mode| to win32 permission bits.
    int win32mode = 0x0;

    if ((mode & R_OK) || (mode & X_OK)) {
        win32mode |= 0x4;
    }
    if (mode & W_OK) {
        win32mode |= 0x2;
    }

    return win32mode;
}
#endif

int path_access(const fs::path& path, int mode) {
#ifdef _WIN32
    // Always use w version, even when UNICODE not defined.
    return _waccess(path.wstring().c_str(), GetWin32Mode(mode));
#else   // !_WIN32
    return HANDLE_EINTR(access(path.c_str(), mode));
#endif  // !_WIN32
}

}  // namespace

bool can_read(const fs::path& path) {
    return path_access(path, R_OK) == 0;
}

bool can_write(const fs::path& path) {
    return path_access(path, W_OK) == 0;
}

bool can_exec(const fs::path& path) {
    return path_access(path, X_OK) == 0;
}

absl::StatusOr<StorageCapacity> file_size(const fs::path& path) {
    std::error_code ec;
    if (std::uintmax_t size = fs::file_size(path, ec); !ec) {
        return size;
    }
    return absl::InternalError(
            absl::StrCat("Failed to get size of: ", path.string(), " - ", ec.message()));
}

std::vector<fs::path> scan_dir(const fs::path& dirPath, bool fullPath) {
    std::error_code ec;

    std::vector<fs::path> x;
    for (const auto& e : std::filesystem::directory_iterator(dirPath, ec)) {
        if (fullPath) {
            // This will be relative if dirPath is relative.
            x.push_back(e.path());
        } else {
            x.push_back(e.path().filename());
        }
    }
    absl::c_sort(x);

    return x;
}

namespace {
fs::perms octal_mode_to_perms(int octalMode) {
    fs::perms mode = fs::perms::none;

    // Owner permissions
    mode |= (octalMode & 0400) ? fs::perms::owner_read : fs::perms::none;
    mode |= (octalMode & 0200) ? fs::perms::owner_write : fs::perms::none;
    mode |= (octalMode & 0100) ? fs::perms::owner_exec : fs::perms::none;

    // Group permissions
    mode |= (octalMode & 0040) ? fs::perms::group_read : fs::perms::none;
    mode |= (octalMode & 0020) ? fs::perms::group_write : fs::perms::none;
    mode |= (octalMode & 0010) ? fs::perms::group_exec : fs::perms::none;

    // Others permissions
    mode |= (octalMode & 0004) ? fs::perms::others_read : fs::perms::none;
    mode |= (octalMode & 0002) ? fs::perms::others_write : fs::perms::none;
    mode |= (octalMode & 0001) ? fs::perms::others_exec : fs::perms::none;

    return mode;
}
}  // namespace

absl::Status chmod(const fs::path& path, int octalMode) {
    if (std::error_code ec;
        fs::permissions(path, octal_mode_to_perms(octalMode), fs::perm_options::replace, ec), ec) {
        return absl::InternalError(
                absl::StrCat("Failed to chmod: ", path.string(), " - ", ec.message()));
    }
    return absl::OkStatus();
}

absl::Status mkdir(const fs::path& path, int octalMode) {
    if (std::error_code ec; !fs::create_directory(path, ec)) {
        return absl::InternalError(
                absl::StrCat("Failed to mkdir: ", path.string(), " - ", ec.message()));
    }
    return chmod(path, octalMode);
}

absl::Status mkdir_recursive(const fs::path& path, int octalMode) {
    // We don't use fs::create_directories here as it doesn't set permissions.
    if (fs::exists(path)) {
        return absl::OkStatus();
    }
    if (!path.has_parent_path()) {
        // Hit the root
        return absl::OkStatus();
    }
    if (auto s = mkdir_recursive(path.parent_path(), octalMode); !s.ok()) {
        return s;
    }
    if (!path.has_filename()) {
        // Ignore trailing separator
        return absl::OkStatus();
    }
    return mkdir(path, octalMode);
}

absl::Status rm(const fs::path& path) {
    if (std::error_code ec; fs::remove(path, ec), ec) {
        return absl::InternalError(
                absl::StrCat("Failed to rm: ", path.string(), " - ", ec.message()));
    }
    return absl::OkStatus();
}

absl::Status rm_recursive(const fs::path& path) {
    if (std::error_code ec; fs::remove_all(path, ec), ec) {
        return absl::InternalError(
                absl::StrCat("Failed to rm recursively: ", path.string(), " - ", ec.message()));
    }
    return absl::OkStatus();
}

absl::Status cp_file(const fs::path& from, const fs::path& to, bool overwrite) {
    fs::copy_options opt =
            overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
    if (std::error_code ec; fs::copy_file(from, to, opt, ec), ec) {
        return absl::InternalError(absl::StrCat("Failed to copy_file: ", from.string(), "->",
                                                to.string(), " - ", ec.message()));
    }
    return absl::OkStatus();
}

absl::Status touch(const fs::path& path) {
    std::ofstream f(path);
    if (f) {
        return absl::OkStatus();
    }
    return absl::DataLossError(absl::StrCat("Unable to create file: ", path.string()));
}

}  // namespace android::base::file
