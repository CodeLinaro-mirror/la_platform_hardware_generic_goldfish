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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "goldfish/base/unique_handle.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"

#ifndef O_BINARY
#ifdef _O_BINARY
#define O_BINARY _O_BINARY
#else
#define O_BINARY 0  // If this isn't defined, the platform doesn't need it.
#endif
#endif

namespace android::base::file {

namespace {

struct FdDeleter {
    struct Empty {};
    FdDeleter() = default;
    FdDeleter(Empty) {}
    void operator()(int fd) const { ::close(fd); }
};

using ScopedFd = goldfish::base::UniqueHandle<int, -1, FdDeleter>;

absl::Status WriteToFd(int fd, std::string_view content, const fs::path& path) {
    size_t total_written = 0;
    while (total_written < content.size()) {
        const ssize_t n = write(fd, content.data() + total_written, content.size() - total_written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return absl::InternalError(absl::StrCat(
                    "Failed to write to '", path.string(), "'. System error: ", strerror(errno),
                    ". Please check if the disk is full or permissions changed."));
        }
        if (n == 0) {
            return absl::InternalError(absl::StrCat("Failed to write to '", path.string(),
                                                    "'. Zero bytes written (unexpected EOF)."));
        }
        total_written += n;
    }
    return absl::OkStatus();
}

}  // namespace

absl::Status CreatePrivateFileExclusive(const fs::path& path, std::string_view content) noexcept {
    // Direct open with O_EXCL ensures atomicity and failure if the file already exists.
    ScopedFd fd(open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_BINARY, 0600));
    if (!fd.ok()) {
        if (errno == EEXIST) {
            return absl::AlreadyExistsError(absl::StrCat(
                    "The file '", path.string(),
                    "' already exists and cannot be overwritten for security reasons."));
        }
        if (errno == EACCES) {
            return absl::PermissionDeniedError(
                    absl::StrCat("Permission denied when creating '", path.string(),
                                 "'. Ensure you have write permissions for the parent directory."));
        }
        return absl::InternalError(absl::StrCat("Failed to create '", path.string(),
                                                "'. System error: ", strerror(errno), "."));
    }

    auto status = WriteToFd(fd.get(), content, path);
    if (!status.ok()) {
        // If writing failed, we try remove the partially written file.
        fd.reset();
        if (auto s = rm(path); !s.ok()) {
            LOG(WARNING) << "Failed to clean up partially written file" << path << ", due to: " << s
                         << ". You might have to manually remove this file.";
        }
        return status;
    }

    return absl::OkStatus();
}

}  // namespace android::base::file
