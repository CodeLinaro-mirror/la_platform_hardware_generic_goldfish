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
#pragma once

#include <filesystem>
#include <string_view>

#include "absl/status/status.h"

/**
 * @file file_atomic.h
 * @brief Provides utilities for atomic and secure file operations.
 */

namespace fs = std::filesystem;

namespace android::base::file {

/**
 * @brief Atomically creates a new file and writes content with restricted permissions.
 *
 * This function creates a new file at the specified @p path and writes the provided
 * @p content. The operation ensures that the file is created with restricted
 * permissions from the outset, preventing a window where the file might be
 * accessible to other users:
 * - On POSIX: The file is created with mode 0600 (read/write for owner only).
 * - On Windows: The file is created with a Security Descriptor allowing access
 *   only to the current user and the Local System account.
 *
 * The creation is atomic in that it will fail if the file already exists,
 * preventing race conditions or accidental overwrites. If an error occurs
 * during the write process, the partially written file will be deleted.
 *
 * @note This function does not automatically create parent directories. If the
 * parent directory does not exist, the operation will fail.
 *
 * @param path The destination path for the new file.
 * @param content The data to be written into the file.
 *
 * @return absl::OkStatus() on success.
 * @return absl::AlreadyExistsError if a file already exists at @p path.
 * @return absl::PermissionDeniedError if the caller lacks necessary permissions
 *         in the destination directory.
 * @return absl::InternalError for other OS-level failures (e.g., disk full,
 *         parent directory missing).
 */
[[nodiscard]] absl::Status CreatePrivateFileExclusive(const fs::path& path,
                                                      std::string_view content) noexcept;

}  // namespace android::base::file
