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
#include <optional>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include "goldfish/file/file_lock.h"
#include "goldfish/file/shared_file_lock.h"

namespace android::goldfish {

struct AvdLockOptions {
    absl::Duration snapshot_lock_timeout = absl::Seconds(10);
    absl::Duration hardware_ini_lock_timeout = absl::Seconds(2);
    absl::Duration multi_instance_lock_timeout = absl::Milliseconds(3500);
    android::base::file::FileLockOptions file_lock_options{};
    android::base::file::SharedFileLockOptions shared_lock_options{};
};

// Coordinates cross-process AVD locking compatible with Android Studio and
// legacy QEMU2 (`snapshot.lock.lock`, `hardware-qemu.ini.lock`, and
// `multiinstance.lock`).
class AvdSessionLock {
  public:
    static absl::StatusOr<AvdSessionLock> Acquire(const std::filesystem::path& avd_content_dir,
                                                  std::string_view avd_name,
                                                  const AvdLockOptions& options = {});

    AvdSessionLock(AvdSessionLock&& other) noexcept = default;
    AvdSessionLock& operator=(AvdSessionLock&& other) = delete;
    AvdSessionLock(const AvdSessionLock&) = delete;
    AvdSessionLock& operator=(const AvdSessionLock&) = delete;

    // Acquires `<avd>/snapshot.lock` during graceful Quickboot shutdown so a
    // rapid restart waits up to `snapshot_lock_timeout` for QEMU's exit-time
    // `-savevm` and session lock release to finish. Callers should destroy the
    // returned lock AFTER destroying this `AvdSessionLock`.
    std::optional<android::base::file::ScopedFileLock> AcquireShutdownSnapshotLock() const;

  private:
    AvdSessionLock(std::filesystem::path avd_content_dir,
                   android::base::file::FileLockOptions file_lock_options,
                   android::base::file::ScopedFileLock hardware_ini_lock,
                   android::base::file::ScopedSharedFileLock multi_instance_lock);

    std::filesystem::path avd_content_dir_;
    android::base::file::FileLockOptions file_lock_options_;
    android::base::file::ScopedFileLock hardware_ini_lock_;
    android::base::file::ScopedSharedFileLock multi_instance_lock_;
};

}  // namespace android::goldfish
