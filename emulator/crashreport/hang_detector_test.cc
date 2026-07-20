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
#include "emulator/plugin/vminterface/test/vm_mock.h"
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
                      std::make_unique<android::base::AbseilClock>())) {
        mock_runstate_set(RUN_STATE_RUNNING);
    }

    void TearDown() override {
        if (mHangDetector) {
            mHangDetector->Stop();
        }
    }

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

    mHangDetector->RemoveWatchedLooper(*event_loop);
}

TEST_F(HangDetectorTest, HangDetectorDestroyedFirst) {
    auto event_loop =
            goldfish::async::ThreadedEventLoop::Create(goldfish::async::LibuvEventLoop::Create());

    mHangDetector->AddWatchedLooper("test loop", *event_loop, absl::Seconds(1));

    EXPECT_FALSE(wait_for_hang());

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
    ASSERT_TRUE(wait_for_hang());

    // Unblock the loop so that it actually terminates!
    hang.Notify();

    mHangDetector->RemoveWatchedLooper(*event_loop);

    // Wait for loop to shutdown as the hang task is referencing the hang notification which gets
    // destroyed before the loop.
    event_loop->ShutdownAndWait().IgnoreError();
}

}  // namespace android::crashreport
