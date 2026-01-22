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
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "absl/status/status.h"

#include "goldfish/memory/shared_memory.h"

// Helper macro to handle EINTR
#define HANDLE_EINTR(x)                                     \
    ({                                                      \
        decltype(x) __eintr_result__;                       \
        do {                                                \
            __eintr_result__ = (x);                         \
        } while (__eintr_result__ == -1 && errno == EINTR); \
        __eintr_result__;                                   \
    })

#define HANDLE_EINTR_RET(ret, x) \
    do {                         \
        (ret) = (x);             \
    } while ((ret) == -1 && errno == EINTR)

namespace goldfish::memory {

using namespace std::string_view_literals;

SharedMemory::SharedMemory(std::string_view path_or_uri, size_t size, DestructionPolicy policy)
        : size_(size), destruction_policy_(policy) {
    static constexpr std::string_view kFileUri = "file://"sv;
    static constexpr std::string_view kLocalhost = "localhost/"sv;

    if (path_or_uri.starts_with(kFileUri)) {
        path_or_uri.remove_prefix(kFileUri.length());
        if (path_or_uri.starts_with(kLocalhost)) {
            path_or_uri.remove_prefix(kLocalhost.length() - 1);  // Remove "localhost", keep "/"
        }
    }
    // Treat name as a file path directly.
    backing_file_ = std::filesystem::path(path_or_uri).lexically_normal().string();
}

absl::Status SharedMemory::Create(std::filesystem::perms mode) {
    return OpenInternal(O_CREAT | O_RDWR | O_EXCL, static_cast<int>(mode));
}

absl::Status SharedMemory::CreateNoMapping(std::filesystem::perms mode) {
    return OpenInternal(O_CREAT | O_RDWR | O_EXCL, static_cast<int>(mode), false);
}

absl::Status SharedMemory::Open(AccessMode access) {
    const int oflag = (access == AccessMode::kReadOnly) ? O_RDONLY : O_RDWR;
    return OpenInternal(oflag, 0);
}

void SharedMemory::Close() {
    if (address_ != nullptr) {
        munmap(address_, size_);
        address_ = nullptr;
    }
    if (fd_ != kInvalidHandle) {
        ::close(fd_);
        fd_ = kInvalidHandle;
    }

    assert(!IsOpen());

    bool should_unlink = (create_ && destruction_policy_ == DestructionPolicy::kAuto) ||
                         (destruction_policy_ == DestructionPolicy::kDestroy);

    if (destruction_policy_ == DestructionPolicy::kKeep) {
        should_unlink = false;
    }

    if (should_unlink) {
        std::filesystem::remove(backing_file_);
    }
}

bool SharedMemory::IsOpen() const {
    return fd_ != kInvalidHandle;
}

absl::Status SharedMemory::OpenInternal(int oflag, int mode, bool do_mapping) {
    if (IsOpen()) {
        return absl::AlreadyExistsError("Shared memory already open");
    }

    const bool create = (oflag & O_CREAT) != 0;
    const int fd = ::open(backing_file_.c_str(), oflag, mode);

    if (fd == -1) {
        return absl::ErrnoToStatus(errno, "Failed to open shared memory");
    }

    if (create) {
        struct stat st;
        const int fstat_res = HANDLE_EINTR(fstat(fd, &st));
        if (fstat_res == -1) {
            const int err = errno;
            ::close(fd);
            return absl::ErrnoToStatus(err, "Failed to stat shared memory");
        }
        if (std::cmp_less(st.st_size, size_)) {
            const int ftruncate_res = HANDLE_EINTR(ftruncate(fd, size_));
            if (ftruncate_res == -1) {
                const int err = errno;
                ::close(fd);
                return absl::ErrnoToStatus(err, "Failed to resize shared memory");
            }
        }
    } else {
        // Verify size for existing segments.
        struct stat st;
        const int fstat_res = HANDLE_EINTR(fstat(fd, &st));
        if (fstat_res == -1) {
            const int err = errno;
            ::close(fd);
            return absl::ErrnoToStatus(err, "Failed to stat shared memory");
        }
        if (std::cmp_less(st.st_size, size_)) {
            ::close(fd);
            return absl::FailedPreconditionError("Shared memory size mismatch: too small");
        }
    }

    // Map the file into memory.
    if (do_mapping) {
        int prot = PROT_READ;
        if ((oflag & O_ACCMODE) == O_RDWR) {
            prot |= PROT_WRITE;
        }
        void* addr = mmap(nullptr, size_, prot, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
            const int err = errno;
            ::close(fd);
            return absl::ErrnoToStatus(err, "Failed to mmap shared memory");
        }
        address_ = addr;
    }
    fd_ = fd;
    create_ = create;
    return absl::OkStatus();
}

}  // namespace goldfish::memory
