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

#include "android/filesystems/ext4_resize.h"

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

static auto convertBytesToMB(uint64_t size) -> unsigned {
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
void explainSystemErrors(const char* msg) {
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

static auto runExt4Program(std::filesystem::path executable,
                           std::initializer_list<std::string> params) -> int {
    if (executable.empty()) {
        LOG(ERROR) << "Couldn't get path to " << executable << " binary";
        return -1;
    }

    std::vector<std::string> commandLine{executable.string()};
    commandLine.insert(commandLine.end(), params);

    auto proc = android::base::Command::Create(commandLine).Execute();
    auto exitCode = proc->ExitCode();

    if (exitCode != 0) {
        LOG(ERROR) << "Resizing partition " << executable << " failed with exit code " << exitCode;
        return exitCode;
    }
    return 0;
}

auto resizeExt4Partition(std::filesystem::path binary_path, const char* partitionPath,
                         int64_t newByteSize) -> int {
    // sanity checks
    if (partitionPath == nullptr || !checkExt4PartitionSize(newByteSize)) {
        return -1;
    }

    // resize2fs requires that we run e2fsck first in order to make sure that
    // the filesystem is in good shape. If we resize without first running this
    // the guest kernel could decide to replay the journal and end up in a state
    // before the resize took place. This is something that frequently happened
    // and caused the resize to not be visible in the guest system.
    int fsckReturnCode = runExt4Program(binary_path / "e2fsck", {"-y", partitionPath});
    if (fsckReturnCode != 0) {
        return fsckReturnCode;
    }

    char size_in_MB[50];
    int copied = snprintf(size_in_MB, sizeof(size_in_MB), "%uM", convertBytesToMB(newByteSize));
    size_in_MB[sizeof(size_in_MB) - 1] = '\0';
    if (copied < 0 || static_cast<size_t>(copied) >= sizeof(size_in_MB)) {
        LOG(ERROR) << "failed to format size in resize2fs command";
        return -1;
    }

    return runExt4Program(binary_path / "resize2fs", {"-f", partitionPath, size_in_MB});
}

auto checkExt4PartitionSize(int64_t byteSize) -> bool {
    uint64_t maxSizeMB = 16 * 1024 * 1024;  // (16 TiB) * (1024 GiB / TiB) * (1024 MiB / GiB)
    uint64_t minSizeMB = 128;
    uint64_t sizeMB = convertBytesToMB(byteSize);

    // compiler converts signed to unsigned
    return (sizeMB >= minSizeMB) && (sizeMB <= maxSizeMB);
}
