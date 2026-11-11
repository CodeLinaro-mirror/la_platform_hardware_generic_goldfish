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

#include "avd_session_lock.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "host-common/constants.h"

namespace android::goldfish {
namespace {

template <typename LockT>
absl::StatusOr<LockT> MapUnavailableLockError(absl::StatusOr<LockT> lock,
                                              std::string_view unavailable_message) {
    if (!lock.ok() && absl::IsUnavailable(lock.status())) {
        return absl::UnavailableError(
                absl::StrFormat("%s (%s)", unavailable_message, lock.status().message()));
    }
    return lock;
}

}  // namespace

AvdSessionLock::AvdSessionLock(std::filesystem::path avd_content_dir,
                               android::base::file::FileLockOptions file_lock_options,
                               android::base::file::ScopedFileLock hardware_ini_lock,
                               android::base::file::ScopedSharedFileLock multi_instance_lock)
        : avd_content_dir_(std::move(avd_content_dir))
        , file_lock_options_(std::move(file_lock_options))
        , hardware_ini_lock_(std::move(hardware_ini_lock))
        , multi_instance_lock_(std::move(multi_instance_lock)) {}

// static
absl::StatusOr<AvdSessionLock> AvdSessionLock::Acquire(const std::filesystem::path& avd_content_dir,
                                                       std::string_view avd_name,
                                                       const AvdLockOptions& options) {
    if (!android::base::file::exists(avd_content_dir)) {
        RETURN_IF_ERROR(android::base::file::mkdir_recursive(avd_content_dir, 0755));
    }

    ASSIGN_OR_RETURN(
            auto snapshot_lock,
            MapUnavailableLockError(
                    android::base::file::ScopedFileLock::Acquire(avd_content_dir / SNAPSHOT_LOCK,
                                                                 options.snapshot_lock_timeout,
                                                                 options.file_lock_options),
                    absl::StrFormat("A snapshot operation for '%s' is pending and timeout has "
                                    "expired. Exiting...",
                                    avd_name)));

    ASSIGN_OR_RETURN(auto hw_lock,
                     MapUnavailableLockError(
                             android::base::file::ScopedFileLock::Acquire(
                                     avd_content_dir / CORE_HARDWARE_INI,
                                     options.hardware_ini_lock_timeout, options.file_lock_options),
                             "Another emulator instance is running. Please close it."));

    ASSIGN_OR_RETURN(
            auto multi_lock,
            MapUnavailableLockError(
                    android::base::file::ScopedSharedFileLock::Acquire(
                            avd_content_dir / MULTIINSTANCE_LOCK,
                            android::base::file::LockShareMode::kExclusiveWrite,
                            options.multi_instance_lock_timeout, options.shared_lock_options),
                    "Another emulator instance is running. Please close it."));

    // `snapshot_lock` releases `<avd>/snapshot.lock.lock` as it goes out of scope.
    return AvdSessionLock(avd_content_dir, options.file_lock_options, std::move(hw_lock),
                          std::move(multi_lock));
}

std::optional<android::base::file::ScopedFileLock> AvdSessionLock::AcquireShutdownSnapshotLock()
        const {
    auto lock = android::base::file::ScopedFileLock::Acquire(
            avd_content_dir_ / SNAPSHOT_LOCK, absl::ZeroDuration(), file_lock_options_);
    if (lock.ok()) {
        return *std::move(lock);
    }
    LOG(WARNING) << "Could not acquire shutdown snapshot lock for '" << avd_content_dir_.string()
                 << "': " << lock.status();
    return std::nullopt;
}

}  // namespace android::goldfish
