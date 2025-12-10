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

#pragma once

#include <filesystem>

#include "absl/status/status.h"

#include "android/base/storage_capacity.h"

namespace fs = std::filesystem;

namespace android::base::file {

absl::StatusOr<fs::path> make_absolute(const fs::path& path);
absl::StatusOr<fs::path> make_relative(const fs::path& path, const fs::path& base_path);
absl::StatusOr<fs::path> make_canonical(const fs::path& path);

bool exists(const fs::path& path);

bool is_file(const fs::path& path);
bool is_dir(const fs::path& path);
bool is_link(const fs::path& path);

bool can_read(const fs::path& path);
bool can_write(const fs::path& path);
bool can_exec(const fs::path& path);

absl::StatusOr<StorageCapacity> file_size(const fs::path& path);

std::vector<fs::path> scan_dir(const fs::path& dirPath, bool fullPath = false);

absl::StatusOr<unsigned> mode(const fs::path& path);

// TODO(b/465404199): Consider changing API to take mode as fs::perms instead of int.
absl::Status chmod(const fs::path& path, unsigned octalMode);

absl::Status mkdir(const fs::path& path, unsigned octalMode);
absl::Status mkdir_recursive(const fs::path& path, unsigned octalMode);

absl::Status rm(const fs::path& path);
absl::Status rm_recursive(const fs::path& path);

absl::Status cp_file(const fs::path& from, const fs::path& to, bool overwrite = false);

absl::Status mv_file(const fs::path& from, const fs::path& to);

absl::Status touch(const fs::path& path);

}  // namespace android::base::file