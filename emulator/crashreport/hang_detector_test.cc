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

#include <chrono>
#include <string_view>
#include <thread>

#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include "android/base/testing/test_clock.h"
#include "android/crashreport/debug.h"
#include "emulator/plugin/vminterface/test/vm_mock.h"
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
        mock_runstate_set(RUN_STATE_RUNNING);
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

TEST_F(HangDetectorTest, PredicateTriggersHang) {
    if (android::base::IsDebuggerAttached()) {
        GTEST_SKIP() << "This test cannot be run under a debugger";
    }
    mHangDetector->AddPredicateCheck([] { return true; }, "Always dead");
    ASSERT_TRUE(wait_for_hang(absl::Seconds(1)));
}

TEST_F(HangDetectorTest, NormalLoopNoHang) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    EXPECT_FALSE(wait_for_hang(absl::Milliseconds(20)));

    mHangDetector->RemoveWatchedLooper(*event_loop);
}

TEST_F(HangDetectorTest, HangDetectorDestroyedFirst) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    EXPECT_FALSE(wait_for_hang(absl::Milliseconds(20)));

    mHangDetector->Stop();
    mHangDetector.reset();
}

TEST_F(HangDetectorTest, BlockedLoopTriggersHang) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    // Add a hanging task
    absl::Notification hang;
    event_loop->Post([&hang] { hang.WaitForNotification(); }).IgnoreError();

    // 1st advance: initial looper check (reschedules with task running)
    mTestClock->Advance(absl::Milliseconds(1100));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // 2nd advance: 1st hang check (hang_count_ -> 1)
    mTestClock->Advance(absl::Milliseconds(1100));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // 3rd advance: 2nd hang check (hang_count_ -> 2, triggers callback)
    mTestClock->Advance(absl::Milliseconds(1100));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    ASSERT_TRUE(wait_for_hang(absl::Seconds(1)));

    // Unblock the loop so that it actually terminates!
    hang.Notify();

    mHangDetector->RemoveWatchedLooper(*event_loop);

    // Wait for loop to shutdown as the hang task is referencing the hang notification which gets
    // destroyed before the loop.
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

    auto test_clock = std::make_unique<android::base::TestClock>();
    auto* clock_ptr = test_clock.get();

    // Create a HangDetector where the hang callback attempts to call RemoveWatchedLooper
    // asynchronously from a separate thread, simulating concurrent teardown.
    auto hang_detector = HangDetector::Create(
            [&](std::string_view msg) {
                std::thread remove_thread([&]() {
                    event_loop->ShutdownAndWait().IgnoreError();
                    if (!remove_completed.HasBeenNotified()) {
                        remove_completed.Notify();
                    }
                });
                remove_thread.detach();
                if (!hang_cb_called.HasBeenNotified()) {
                    hang_cb_called.Notify();
                }
            },
            {
                .hang_loop_iteration_timeout = absl::Milliseconds(5),
                .hang_check_timeout = absl::Milliseconds(100),
            },
            std::move(test_clock));

    hang_detector->AddWatchedLooper("test loop", *event_loop, absl::Milliseconds(100));

    // Block the event loop to trigger hang detection
    absl::Notification hang;
    event_loop->Post([&hang] { hang.WaitForNotification(); }).IgnoreError();

    // 1st advance: initial check
    clock_ptr->Advance(absl::Milliseconds(110));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // 2nd advance: 1st hang check
    clock_ptr->Advance(absl::Milliseconds(110));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // 3rd advance: 2nd hang check
    clock_ptr->Advance(absl::Milliseconds(110));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // Verify hang callback fires and async thread can run without deadlocking on HangDetector mutex
    ASSERT_TRUE(hang_cb_called.WaitForNotificationWithTimeout(absl::Seconds(1)));

    hang.Notify();
    ASSERT_TRUE(remove_completed.WaitForNotificationWithTimeout(absl::Seconds(1)));
    hang_detector->Stop();
}

TEST_F(HangDetectorTest, RemoveWatchedLooperAfterStopNoCrash) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    // Stop the detector first (simulating early teardown)
    mHangDetector->Stop();

    // Verify calling RemoveWatchedLooper after Stop() is safe and does not crash or assert
    EXPECT_NO_FATAL_FAILURE(mHangDetector->RemoveWatchedLooper(*event_loop));

    event_loop->ShutdownAndWait().IgnoreError();
}

}  // namespace android::crashreport
