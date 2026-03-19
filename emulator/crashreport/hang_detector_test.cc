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
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include "android/base/abseil_clock.h"
#include "android/crashreport/debug.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

namespace android::crashreport {

class HangDetectorTest : public ::testing::Test {
  public:
    HangDetectorTest()
            : mHangDetector(HangDetector::Create(
                      [this](std::string_view msg) {
                          if (!mNotify.HasBeenNotified()) {
                              mNotify.Notify();
                          }
                      },
                      {
                          .hang_loop_iteration_timeout = absl::Milliseconds(100),
                          .hang_check_timeout = absl::Milliseconds(1000),
                      },
                      std::make_unique<android::base::AbseilClock>())) {}

    void TearDown() override { mHangDetector->Stop(); }

    bool wait_for_hang() { return mNotify.WaitForNotificationWithTimeout(kMaxBlockingTime); }

  protected:
    const absl::Duration kMaxBlockingTime = absl::Seconds(10);
    absl::Notification mNotify;
    std::unique_ptr<HangDetector> mHangDetector;
};

TEST_F(HangDetectorTest, PredicateTriggersHang) {
    if (android::base::IsDebuggerAttached()) {
        printf("This test cannot be run under a debugger.");
        return;
    }
    mHangDetector->AddPredicateCheck([] { return true; }, "Always dead");
    ASSERT_TRUE(wait_for_hang());
}

TEST_F(HangDetectorTest, NormalLoopNoHang) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    EXPECT_FALSE(wait_for_hang());
}

TEST_F(HangDetectorTest, BlockedLoopTriggersHang) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    // Add a hanging task
    absl::Notification hang;
    event_loop->Post([&hang] { hang.WaitForNotification(); });
    ASSERT_TRUE(wait_for_hang());

    // Unblock the loop so that it actually terminates!
    hang.Notify();

    // Wait for loop to shutdown as the hang task is referencing the hang notification which gets
    // destroyed before the loop.
    event_loop->ShutdownAndWait();
}

TEST_F(HangDetectorTest, LoopDisappearsBeforeHangNoCrash) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    // Note no hanging task.

    // Delete the event loop while the hang detector is still running.
    event_loop.reset();

    ASSERT_TRUE(wait_for_hang());
}

TEST_F(HangDetectorTest, LoopDisappearsAfterHangNoCrash) {
    // Note test loop has to be used as trying to destroy the uv loop hangs waiting for all tasks to
    // complete.
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    // Add a hanging task
    absl::Notification hang;
    event_loop->Post([&hang] { hang.WaitForNotification(); });

    ASSERT_TRUE(wait_for_hang());

    auto f = event_loop->Shutdown();

    // Unblock the loop so that it actually terminates!
    hang.Notify();

    ASSERT_THAT(f.get(), absl_testing::IsOk());

    // Delete the event loop while the hang detector is still running.
    event_loop.reset();
}

}  // namespace android::crashreport
