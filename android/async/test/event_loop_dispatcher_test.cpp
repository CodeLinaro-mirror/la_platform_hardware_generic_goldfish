// Copyright (C) 2025 The Android Open Source Project
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
#include "goldfish/async/event_loop_dispatcher.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "absl/synchronization/notification.h"

#include "aemu/base/events/EventSource.h"
#include "aemu/base/events/WithCallbacks.h"
#include "aemu/base/events/policies/HybridStoragePolicy.h"
#include "goldfish/async/libuv_event_loop.h"

using namespace std::chrono_literals;

namespace {

// Explicitly import the types we need to improve clarity.
using android::base::eventing::EventListener;
using android::base::eventing::EventSource;
using android::base::eventing::HybridStoragePolicy;
using android::base::eventing::makeScopedCallback;
using android::base::eventing::WithCallbacks;
using goldfish::async::EventLoop;
using goldfish::async::EventLoopDispatcher;
using goldfish::async::LibuvEventLoop;

// 1. Define an event and a listener, local to this test file.
struct TestEvent {
    int value;
    std::thread::id origin_thread;
};

class TestListener : public EventListener<TestEvent> {
  public:
    TestListener(EventLoop* loop, absl::Notification* notification)
            : mLoop(loop), mNotification(notification) {}

    void eventArrived(const TestEvent& event) override {
        // This check confirms the event is handled on the correct thread.
        EXPECT_TRUE(mLoop->isOnLoopThread());
        mLastValue = event.value;
        mNotification->Notify();
    }

    int lastValue() const { return mLastValue; }

  private:
    EventLoop* mLoop;
    absl::Notification* mNotification;
    int mLastValue = 0;
};

// Define a clear alias for the complex EventSource type used in the tests.
// This source is bound to an EventLoop and uses a HybridStoragePolicy
// with std::shared_ptr for memory-safe listener management.
using LoopBoundSafeSource = goldfish::async::LoopBoundSafeSource<TestEvent>;

// An alias for a source that includes the WithCallbacks mixin for a modern API.
using CallbackSource = goldfish::async::LoopBoundCallbackSource<TestEvent>;

}  // namespace

TEST(EventLoopDispatcherTest, ScopedCallbackIsAutomaticallyUnregistered) {
    // 1. Create an EventLoop and run it.
    auto eventLoop = LibuvEventLoop::create();
    std::thread loopThread([&]() { (void)eventLoop->run(); });

    // 2. Create a source that supports the callback API.
    CallbackSource callbackSource(eventLoop.get());
    absl::Notification event_received;
    int received_value = 0;

    // 3. Create a scoped callback. It will be automatically unregistered
    //    when `scoped_handle` goes out of scope.
    {
        auto scoped_handle = makeScopedCallback(callbackSource, [&](const TestEvent& event) {
            EXPECT_TRUE(eventLoop->isOnLoopThread());
            received_value = event.value;
            event_received.Notify();
        });

        // 4. Fire an event. The callback should be active.
        callbackSource.fireEvent({100, std::this_thread::get_id()});
        ASSERT_TRUE(event_received.WaitForNotificationWithTimeout(absl::Seconds(2)));
        EXPECT_EQ(received_value, 100);
        EXPECT_EQ(callbackSource.callbackCount(), 1);
    }  // <-- `scoped_handle` is destroyed here.

    // 5. The callback should now be unregistered.
    EXPECT_EQ(callbackSource.callbackCount(), 0);

    // 6. Fire the event again. The notification should not be triggered.
    absl::Notification event_received_again;
    callbackSource.fireEvent({200, std::this_thread::get_id()});
    EXPECT_FALSE(event_received_again.WaitForNotificationWithTimeout(absl::Milliseconds(50)));
    EXPECT_EQ(received_value, 100);  // The value should not have changed.

    // 7. Clean up.
    (void)eventLoop->shutdownAndWait(500ms);
    loopThread.join();
}
