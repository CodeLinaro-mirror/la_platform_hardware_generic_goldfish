// Copyright 2018 The Android Open Source Project
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

#include "android/crashreport/hang_detector.h"

#include <string_view>

#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include "android/base/testing/test_clock.h"
#include "android/crashreport/debug.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

namespace android::crashreport {

class HangDetectorTest : public ::testing::Test {
  public:
    HangDetectorTest() {
        auto clock = std::make_unique<android::base::TestClock>();
        mTestClock = clock.get();
        mHangDetector = HangDetector::Create(
                [this](std::string_view msg) {
                    if (!mNotify.HasBeenNotified()) {
                        mNotify.Notify();
                    }
                },
                {
                    .hang_loop_iteration_timeout = absl::Milliseconds(5),
                    .hang_check_timeout = absl::Milliseconds(1000),
                },
                std::move(clock));
    }

    void TearDown() override {
        if (mHangDetector) {
            mHangDetector->Stop();
        }
    }

    bool wait_for_hang(absl::Duration timeout = absl::Milliseconds(50)) {
        return mNotify.WaitForNotificationWithTimeout(timeout);
    }

  protected:
    android::base::TestClock* mTestClock = nullptr;
    absl::Notification mNotify;
    std::unique_ptr<HangDetector> mHangDetector;
};

TEST_F(HangDetectorTest, NormalLoopNoHang) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1),
                                    []() { return true; });

    EXPECT_FALSE(wait_for_hang(absl::Milliseconds(20)));

    mHangDetector->RemoveWatchedLooper(*event_loop);
}

TEST_F(HangDetectorTest, HangDetectorDestroyedFirst) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1),
                                    []() { return true; });

    EXPECT_FALSE(wait_for_hang(absl::Milliseconds(20)));

    mHangDetector->Stop();
    mHangDetector.reset();
}

TEST_F(HangDetectorTest, BlockedLoopTriggersHang) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1),
                                    []() { return true; });

    // Add a hanging task
    absl::Notification hang;
    event_loop->Post([&hang] { hang.WaitForNotification(); }).IgnoreError();

    // 1st advance: initial looper check (reschedules with task running)
    auto end_time = absl::Now() + absl::Seconds(5);
    while (!mNotify.HasBeenNotified() && absl::Now() < end_time) {
        mTestClock->Advance(absl::Milliseconds(100));
        absl::SleepFor(absl::Milliseconds(10));
    }
    ASSERT_TRUE(wait_for_hang(absl::Seconds(10)));

    // Unblock the loop so that it actually terminates!
    hang.Notify();

    mHangDetector->RemoveWatchedLooper(*event_loop);

    // Wait for loop to shutdown as the hang task is referencing the hang notification which gets
    // destroyed before the loop.
    event_loop->ShutdownAndWait().IgnoreError();
}

TEST_F(HangDetectorTest, BlockedLoopIgnoredWhenVmStopped) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    // Register looper with is_vm_running predicate returning false (VM stopped)
    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1),
                                    []() { return false; });

    // Add a hanging task
    absl::Notification hang;
    event_loop->Post([&hang] { hang.WaitForNotification(); }).IgnoreError();

    // Advance clock past the hang timeout repeatedly
    for (int i = 0; i < 20; ++i) {
        mTestClock->Advance(absl::Milliseconds(100));
        absl::SleepFor(absl::Milliseconds(10));
    }

    // Verify hang callback was NOT invoked because the VM was stopped
    EXPECT_FALSE(mNotify.HasBeenNotified());

    // Unblock and clean up
    hang.Notify();
    mHangDetector->RemoveWatchedLooper(*event_loop);
    event_loop->ShutdownAndWait().IgnoreError();
}

TEST_F(HangDetectorTest, NoHangCallbackDeadlockWhenRemovingLooper) {
    if (android::base::IsDebuggerAttached()) {
        GTEST_SKIP() << "This test cannot be run under a debugger";
    }

    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    absl::Notification hang_cb_called;
    absl::Notification remove_completed;

    // Start a teardown thread upfront that waits for the hang callback to fire,
    // simulating concurrent looper removal upon hang detection.
    std::thread remove_thread([&]() {
        hang_cb_called.WaitForNotification();
        event_loop->ShutdownAndWait().IgnoreError();
        remove_completed.Notify();
    });

    auto test_clock = std::make_unique<android::base::TestClock>();
    auto* clock_ptr = test_clock.get();

    // Create a HangDetector where the hang callback triggers asynchronous looper removal.
    auto hang_detector = HangDetector::Create(
            [&](std::string_view msg) {
                if (!hang_cb_called.HasBeenNotified()) {
                    hang_cb_called.Notify();
                }
            },
            {
                .hang_loop_iteration_timeout = absl::Milliseconds(5),
                .hang_check_timeout = absl::Milliseconds(100),
            },
            std::move(test_clock));

    hang_detector->AddWatchedLooper("test loop", *event_loop, absl::Milliseconds(100),
                                    []() { return true; });

    // Block the event loop to trigger hang detection
    absl::Notification hang;
    event_loop->Post([&hang] { hang.WaitForNotification(); }).IgnoreError();

    // 1st advance: initial check
    auto end_time = absl::Now() + absl::Seconds(5);
    while (!hang_cb_called.HasBeenNotified() && absl::Now() < end_time) {
        clock_ptr->Advance(absl::Milliseconds(20));
        absl::SleepFor(absl::Milliseconds(5));
    }
    ASSERT_TRUE(hang_cb_called.WaitForNotificationWithTimeout(absl::Seconds(10)));

    hang.Notify();
    ASSERT_TRUE(remove_completed.WaitForNotificationWithTimeout(absl::Seconds(10)));
    if (remove_thread.joinable()) {
        remove_thread.join();
    }
    hang_detector->Stop();
}

TEST_F(HangDetectorTest, RemoveWatchedLooperAfterStopNoCrash) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1),
                                    []() { return true; });

    // Stop the detector first (simulating early teardown)
    mHangDetector->Stop();

    // Verify calling RemoveWatchedLooper after Stop() is safe and does not crash or assert
    EXPECT_NO_FATAL_FAILURE(mHangDetector->RemoveWatchedLooper(*event_loop));

    event_loop->ShutdownAndWait().IgnoreError();
}

}  // namespace android::crashreport
