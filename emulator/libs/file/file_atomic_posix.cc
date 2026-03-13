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

#include <cstdlib>
#include <cstring>

#include "absl/cleanup/cleanup.h"
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
    // Generate a unique temporary file path cleanly in the same directory.
    std::string tmp_name_template = absl::StrCat(path.string(), ".tmp.XXXXXX");

    // Open with 0600 permissions and close-on-exec flag, tmp_name_template is modified in place.
    ScopedFd fd(mkostemp(tmp_name_template.data(), O_CLOEXEC));
    if (!fd.ok()) {
        return absl::InternalError(absl::StrCat("Failed to write to '", path.string(),
                                                "'. We couldn't create a temporary file in '",
                                                path.parent_path().string(),
                                                "' (error: ", strerror(errno),
                                                "). Please check if you have write permissions "
                                                "and enough disk space."));
    }

    // Setup cleanup to remove the temporary file.
    // We use hard links so we can always remove it.
    absl::Cleanup remove_tmp_file = [tmp_name_template] {
        if (auto s = rm(tmp_name_template); !s.ok()) {
            LOG(WARNING) << "Failed to remove temporary file '" << tmp_name_template
                         << "': " << s.message()
                         << ". You might have to manually remove it to reclaim space.";
        }
    };

    auto status = WriteToFd(fd.get(), content, tmp_name_template);
    if (!status.ok()) {
        return status;
    }

    // We are going to create a hardlink, which means that path will point
    // to the same (refcounted) inode as tmp_name_template, if path already points to something
    // this will fail. Since it is a hardlink we can safely remove tmp_name_template, as it
    // will merely decrease the refcount.
    if (link(tmp_name_template.c_str(), path.c_str()) != 0) {
        int err = errno;
        if (err == EEXIST) {
            return absl::AlreadyExistsError(absl::StrCat(
                    "The file '", path.string(),
                    "' already exists and cannot be overwritten for security reasons."));
        }
        return absl::InternalError(absl::StrCat("Failed to finalize creation of '", path.string(),
                                                "' (error: ", strerror(err),
                                                "). Check permissions or if another process is "
                                                "modifying this directory."));
    }

    return absl::OkStatus();
}

}  // namespace android::base::file
