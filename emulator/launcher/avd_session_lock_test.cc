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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "android/base/testing/test_system.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_lock.h"
#include "goldfish/file/shared_file_lock.h"
#include "goldfish/file/testing/auto_advancing_clock.h"
#include "host-common/constants.h"

namespace android::goldfish {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::android::base::TestSystem;
using ::android::base::file::LockHolderState;
using ::android::base::file::LockShareMode;
using ::android::base::file::ScopedFileLock;
using ::android::base::file::ScopedSharedFileLock;
using ::android::base::file::testing::AutoAdvancingClock;
using ::testing::AllOf;
using ::testing::HasSubstr;

static_assert(std::is_nothrow_move_constructible_v<AvdSessionLock> &&
              !std::is_move_assignable_v<AvdSessionLock>);

AvdLockOptions MakeTestLockOptions(AutoAdvancingClock& clock, int pid_override) {
    return {
        .file_lock_options =
                {
                    .clock = clock,
                    .check_holder_fn = [](int) { return LockHolderState::kAliveEmulator; },
                    .current_pid_override = pid_override,
                },
        .shared_lock_options =
                {
                    .clock = clock,
                },
    };
}

TEST(AvdSessionLockTest, SessionBlocksSecondSessionAndReleasesCleanly) {
    TestSystem sys("bin", "home");
    AutoAdvancingClock clock;
    const std::filesystem::path avd_dir = sys.GetTempRoot()->Path() / "Pixel_9.avd";

    {
        ASSERT_OK_AND_ASSIGN(
                AvdSessionLock session1,
                AvdSessionLock::Acquire(avd_dir, "Pixel_9",
                                        MakeTestLockOptions(clock, /*pid_override=*/1001)));
        EXPECT_TRUE(android::base::file::exists(
                ScopedFileLock::LockPathForTarget(avd_dir / CORE_HARDWARE_INI)));
        // Startup snapshot lock must be released once Acquire() returns.
        EXPECT_FALSE(android::base::file::exists(
                ScopedFileLock::LockPathForTarget(avd_dir / SNAPSHOT_LOCK)));

        EXPECT_THAT(AvdSessionLock::Acquire(avd_dir, "Pixel_9",
                                            MakeTestLockOptions(clock, /*pid_override=*/1002)),
                    StatusIs(absl::StatusCode::kUnavailable,
                             AllOf(HasSubstr("Another emulator instance is running."),
                                   HasSubstr("held by PID 1001"))));
    }

    // After session1 goes out of scope, a second session succeeds.
    ASSERT_OK_AND_ASSIGN(
            AvdSessionLock session2,
            AvdSessionLock::Acquire(avd_dir, "Pixel_9",
                                    MakeTestLockOptions(clock, /*pid_override=*/1002)));
}

TEST(AvdSessionLockTest, MultiInstanceLockContentionRollsBackHardwareIniLock) {
    TestSystem sys("bin", "home");
    AutoAdvancingClock clock;
    const std::filesystem::path avd_dir = sys.GetTempRoot()->Path() / "Pixel_9.avd";
    ASSERT_THAT(android::base::file::mkdir_recursive(avd_dir, 0755), IsOk());

    {
        auto opts = MakeTestLockOptions(clock, /*pid_override=*/2001);
        ASSERT_OK_AND_ASSIGN(ScopedSharedFileLock external_multi_lock,
                             ScopedSharedFileLock::Acquire(
                                     avd_dir / MULTIINSTANCE_LOCK, LockShareMode::kSharedRead,
                                     absl::ZeroDuration(), opts.shared_lock_options));

        // Session must be rejected while multiinstance.lock is held externally.
        EXPECT_THAT(AvdSessionLock::Acquire(avd_dir, "Pixel_9",
                                            MakeTestLockOptions(clock, /*pid_override=*/2002)),
                    StatusIs(absl::StatusCode::kUnavailable,
                             HasSubstr("Another emulator instance is running.")));
        // Failed session must also roll back its hardware-qemu.ini.lock!
        EXPECT_FALSE(android::base::file::exists(
                ScopedFileLock::LockPathForTarget(avd_dir / CORE_HARDWARE_INI)));
    }

    ASSERT_THAT(AvdSessionLock::Acquire(avd_dir, "Pixel_9",
                                        MakeTestLockOptions(clock, /*pid_override=*/2003)),
                IsOk());
}

TEST(AvdSessionLockTest, PendingSnapshotOperationReportsTimeoutError) {
    TestSystem sys("bin", "home");
    AutoAdvancingClock clock;
    const std::filesystem::path avd_dir = sys.GetTempRoot()->Path() / "Pixel_9.avd";
    ASSERT_THAT(android::base::file::mkdir_recursive(avd_dir, 0755), IsOk());

    auto pending_opts = MakeTestLockOptions(clock, /*pid_override=*/4001);
    ASSERT_OK_AND_ASSIGN(ScopedFileLock external_snap_lock,
                         ScopedFileLock::Acquire(avd_dir / SNAPSHOT_LOCK, absl::ZeroDuration(),
                                                 pending_opts.file_lock_options));

    EXPECT_THAT(AvdSessionLock::Acquire(avd_dir, "Pixel_9",
                                        MakeTestLockOptions(clock, /*pid_override=*/4002)),
                StatusIs(absl::StatusCode::kUnavailable,
                         AllOf(HasSubstr("A snapshot operation for 'Pixel_9' is pending"),
                               HasSubstr("held by PID 4001"))));
}

TEST(AvdSessionLockTest, ShutdownSnapshotLockSerializesRapidRestartHandoff) {
    TestSystem sys("bin", "home");
    const std::filesystem::path avd_dir = sys.GetTempRoot()->Path() / "Pixel_9.avd";

    AvdLockOptions opts1;
    opts1.file_lock_options.check_holder_fn = [](int) { return LockHolderState::kAliveEmulator; };
    opts1.file_lock_options.current_pid_override = 5001;

    // Declared before `first_session` so C++ reverse-order destruction releases
    // `shutdown_lock` AFTER `first_session` unlocks `multiinstance.lock` and
    // `hardware-qemu.ini.lock` (matching `Launcher`'s member layout).
    std::optional<ScopedFileLock> shutdown_lock;
    ASSERT_OK_AND_ASSIGN(AvdSessionLock acquired_session,
                         AvdSessionLock::Acquire(avd_dir, "Pixel_9", opts1));
    std::optional<AvdSessionLock> first_session(std::move(acquired_session));

    // Simulate graceful Quickboot shutdown taking snapshot.lock.
    shutdown_lock = first_session->AcquireShutdownSnapshotLock();
    ASSERT_TRUE(shutdown_lock.has_value());
    EXPECT_TRUE(android::base::file::exists(
            ScopedFileLock::LockPathForTarget(avd_dir / SNAPSHOT_LOCK)));

    absl::Notification second_started;
    absl::Notification second_done;
    absl::Status second_status;

    std::thread restart_thread([&]() {
        AvdLockOptions opts2;
        opts2.snapshot_lock_timeout = absl::Seconds(5);
        opts2.hardware_ini_lock_timeout = absl::ZeroDuration();
        opts2.multi_instance_lock_timeout = absl::ZeroDuration();
        opts2.file_lock_options.check_holder_fn = [](int) {
            return LockHolderState::kAliveEmulator;
        };
        opts2.file_lock_options.current_pid_override = 5002;

        second_started.Notify();
        auto res = AvdSessionLock::Acquire(avd_dir, "Pixel_9", opts2);
        second_status = res.status();
        second_done.Notify();
    });

    second_started.WaitForNotification();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(second_done.HasBeenNotified());

    // Unlocking `first_session` BEFORE releasing `shutdown_lock` lets
    // `restart_thread` acquire `hardware-qemu.ini.lock` immediately with zero
    // `hardware_ini_lock_timeout`.
    first_session.reset();
    shutdown_lock.reset();

    const bool second_completed = second_done.WaitForNotificationWithTimeout(absl::Seconds(5));
    restart_thread.join();
    ASSERT_TRUE(second_completed);
    EXPECT_THAT(second_status, IsOk());
}

TEST(AvdSessionLockTest, SupportsNonAsciiAvdDirectory) {
    TestSystem sys("bin", "home");
    AutoAdvancingClock clock;
    const std::filesystem::path avd_dir = sys.GetTempRoot()->Path() / "测试_🚀_é.avd";

    ASSERT_OK_AND_ASSIGN(
            AvdSessionLock session,
            AvdSessionLock::Acquire(avd_dir, "测试_🚀_é",
                                    MakeTestLockOptions(clock, /*pid_override=*/6001)));
    EXPECT_TRUE(android::base::file::is_dir(avd_dir));
}

TEST(AvdSessionLockTest, MoveConstructionTransfersSessionLocksIntact) {
    TestSystem sys("bin", "home");
    AutoAdvancingClock clock;
    const std::filesystem::path avd_dir = sys.GetTempRoot()->Path() / "Avd_Move.avd";

    {
        std::optional<AvdSessionLock> moved;
        {
            ASSERT_OK_AND_ASSIGN(
                    AvdSessionLock lock,
                    AvdSessionLock::Acquire(avd_dir, "Avd_Move",
                                            MakeTestLockOptions(clock, /*pid_override=*/8001)));
            moved.emplace(std::move(lock));
        }
        // Destroying the moved-from `lock` must leave `moved`'s locks intact.
        EXPECT_TRUE(android::base::file::exists(
                ScopedFileLock::LockPathForTarget(avd_dir / CORE_HARDWARE_INI)));
        EXPECT_THAT(AvdSessionLock::Acquire(avd_dir, "Avd_Move",
                                            MakeTestLockOptions(clock, /*pid_override=*/8002)),
                    StatusIs(absl::StatusCode::kUnavailable));
    }

    EXPECT_FALSE(android::base::file::exists(
            ScopedFileLock::LockPathForTarget(avd_dir / CORE_HARDWARE_INI)));
    ASSERT_THAT(AvdSessionLock::Acquire(avd_dir, "Avd_Move",
                                        MakeTestLockOptions(clock, /*pid_override=*/8003)),
                IsOk());
}

}  // namespace
}  // namespace android::goldfish
