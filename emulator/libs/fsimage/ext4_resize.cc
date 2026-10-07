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

#include "android/fsimage/ext4_resize.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include "absl/log/log.h"

#include "android/base/system.h"
#include "android/process/command.h"
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

using android::base::System;

static auto ConvertBytesToMB(uint64_t size) -> unsigned {
    if (size == 0) {
        return 0;
    }
    size = (size + (1ULL << 20) - 1ULL) >> 20;
    if (size > UINT_MAX) {
        size = UINT_MAX;
    }
    return static_cast<unsigned>(size);
}

// Convenience function for formatting and printing system call/library
// function errors that show up regardless of host platform. Equivalent
// to printing the stringified error code from errno or GetLastError()
// (for windows).
void ExplainSystemErrors(const char* msg) {
#ifdef _WIN32
    char* pstr = NULL;
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(),
                  0, (LPTSTR)&pstr, 2, NULL);
    LOG(ERROR) << msg << " - " << pstr;
    LocalFree(pstr);
#else
    PLOG(ERROR) << msg << " - " << strerror(errno);
#endif
}

static auto RunExt4Program(std::filesystem::path executable,
                           std::initializer_list<std::string> params) -> int {
    if (executable.empty()) {
        LOG(ERROR) << "Couldn't get path to " << executable << " binary";
        return -1;
    }

    std::vector<std::string> command_line{executable.string()};
    command_line.insert(command_line.end(), params);

    auto proc = android::base::Command::Create(command_line).Execute();
    auto exit_code = proc->ExitCode();

    if (exit_code != 0) {
        LOG(ERROR) << "Resizing partition " << executable << " failed with exit code " << exit_code;
        return exit_code;
    }
    return 0;
}

auto resizeExt4Partition(std::filesystem::path binary_path, const char* partition_path,
                         int64_t new_byte_size) -> int {
    // sanity checks
    if (partition_path == nullptr || !checkExt4PartitionSize(new_byte_size)) {
        return -1;
    }

    // resize2fs requires that we run e2fsck first in order to make sure that
    // the filesystem is in good shape. If we resize without first running this
    // the guest kernel could decide to replay the journal and end up in a state
    // before the resize took place. This is something that frequently happened
    // and caused the resize to not be visible in the guest system.
    int fsck_return_code = RunExt4Program(binary_path / "e2fsck", {"-y", partition_path});
    if (fsck_return_code != 0) {
        return fsck_return_code;
    }

    char size_in_mb[50];
    int copied = snprintf(size_in_mb, sizeof(size_in_mb), "%uM", ConvertBytesToMB(new_byte_size));
    size_in_mb[sizeof(size_in_mb) - 1] = '\0';
    if (copied < 0 || static_cast<size_t>(copied) >= sizeof(size_in_mb)) {
        LOG(ERROR) << "failed to format size in resize2fs command";
        return -1;
    }

    return RunExt4Program(binary_path / "resize2fs", {"-f", partition_path, size_in_mb});
}

auto checkExt4PartitionSize(int64_t byte_size) -> bool {
    uint64_t max_size_mb = 16ULL * 1024 * 1024;  // (16 TiB) * (1024 GiB / TiB) * (1024 MiB / GiB)
    uint64_t min_size_mb = 128;
    uint64_t size_mb = ConvertBytesToMB(byte_size);

    // compiler converts signed to unsigned
    return (size_mb >= min_size_mb) && (size_mb <= max_size_mb);
}
