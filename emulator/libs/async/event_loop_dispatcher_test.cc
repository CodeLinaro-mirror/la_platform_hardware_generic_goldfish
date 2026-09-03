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
#include <memory>
#include <thread>

#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/eventing/event_source.h"
#include "goldfish/eventing/policies/hybrid_storage_policy.h"
#include "goldfish/eventing/with_callbacks.h"
#include "include/goldfish/async/event_loop.h"

using namespace std::chrono_literals;

namespace goldfish::async::tests {

// Explicitly import the types we need to improve clarity.
using android::base::eventing::EventListener;
using android::base::eventing::EventSource;
using android::base::eventing::HybridStoragePolicy;
using android::base::eventing::MakeScopedCallback;
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
            : loop_(loop), notification_(notification) {}

    void EventArrived(const TestEvent& event) override {
        // This check confirms the event is handled on the correct thread.
        EXPECT_TRUE(loop_->IsOnLoopThread());
        last_value_ = event.value;
        notification_->Notify();
    }

    int lastValue() const { return last_value_; }

  private:
    EventLoop* loop_;
    absl::Notification* notification_;
    int last_value_ = 0;
};

class RunningLoopListener {
  public:
    RunningLoopListener(EventLoop& loop) : loop_(loop) {
        callback_id_ = loop_.AddCallback([this](const LooperStatusEvent& event) {
            if (event.state == LooperStatusEvent::State::kRunning) {
                notification_.Notify();
            }
        });
    }

    ~RunningLoopListener() { loop_.RemoveCallback(callback_id_); }

    const absl::Notification& GetNotification() const { return notification_; }

  private:
    EventLoop& loop_;
    size_t callback_id_;
    absl::Notification notification_;
};

// Define a clear alias for the complex EventSource type used in the tests.
// This source is bound to an EventLoop and uses a HybridStoragePolicy
// with std::shared_ptr for memory-safe listener management.
using LoopBoundSafeSource = goldfish::async::LoopBoundSafeSource<TestEvent>;

// An alias for a source that includes the WithCallbacks mixin for a modern API.
using CallbackSource = goldfish::async::LoopBoundCallbackSource<TestEvent>;

TEST(EventLoopDispatcherTest, EventIsDispatchedOnEventLoopThread) {
    // 1. Create an EventLoop instance and run it in a background thread.
    auto eventLoop = LibuvEventLoop::Create();
    RunningLoopListener rll(*eventLoop);
    std::thread loopThread([&]() { (void)eventLoop->Run(); });
    rll.GetNotification().WaitForNotification();

    // 2. Create the EventSource, passing the event loop to the dispatcher's constructor.
    LoopBoundSafeSource loopBoundSource(eventLoop.get());

    // 3. Add a listener.
    absl::Notification event_received;
    auto listener = std::make_shared<TestListener>(eventLoop.get(), &event_received);
    loopBoundSource.AddListener(listener);

    // 4. Fire an event from the main thread.
    loopBoundSource.FireEvent({42, std::this_thread::get_id()});

    // 5. Wait for the event to be processed.
    ASSERT_TRUE(event_received.WaitForNotificationWithTimeout(absl::Seconds(2)));

    // 6. Verify the listener received the correct value.
    EXPECT_EQ(listener->lastValue(), 42);

    // 7. Cleanly shut down the loop.
    ASSERT_THAT(eventLoop->ShutdownAndWait(), absl_testing::IsOk());
    loopThread.join();
}

TEST(EventLoopDispatcherTest, EventIsDispatchedImmediatelyWhenOnLoopThread) {
    // 1. Create an EventLoop instance and run it in a background thread.
    auto eventLoop = LibuvEventLoop::Create();
    RunningLoopListener rll(*eventLoop);
    std::thread loopThread([&]() { (void)eventLoop->Run(); });
    rll.GetNotification().WaitForNotification();

    // 2. Create the EventSource.
    LoopBoundSafeSource loopBoundSource(eventLoop.get());

    // 3. Add a listener.
    absl::Notification event_received;
    auto listener = std::make_shared<TestListener>(eventLoop.get(), &event_received);
    loopBoundSource.AddListener(listener);

    // 4. Post a task to the event loop to fire the event from there.
    ASSERT_THAT(eventLoop->Post([&]() {
        // Now we are on the loop thread, the dispatch should be immediate.
        loopBoundSource.FireEvent({99, std::this_thread::get_id()});
    }),
                absl_testing::IsOk());

    // 5. Wait for the event to be processed.
    ASSERT_TRUE(event_received.WaitForNotificationWithTimeout(absl::Seconds(2)));

    // 6. Verify the listener received the correct value.
    EXPECT_EQ(listener->lastValue(), 99);

    // 7. Cleanly shut down the loop.
    ASSERT_THAT(eventLoop->ShutdownAndWait(absl::Milliseconds(500)), absl_testing::IsOk());
    loopThread.join();
}

TEST(EventLoopDispatcherTest, ScopedCallbackIsAutomaticallyUnregistered) {
    // 1. Create an EventLoop and run it.
    auto eventLoop = LibuvEventLoop::Create();
    RunningLoopListener rll(*eventLoop);
    std::thread loopThread([&]() { (void)eventLoop->Run(); });
    rll.GetNotification().WaitForNotification();

    // 2. Create a source that supports the callback API.
    CallbackSource callbackSource(eventLoop.get());
    absl::Notification event_received;
    int received_value = 0;

    // 3. Create a scoped callback. It will be automatically unregistered
    //    when `scoped_handle` goes out of scope.
    {
        auto scoped_handle = MakeScopedCallback(callbackSource, [&](const TestEvent& event) {
            EXPECT_TRUE(eventLoop->IsOnLoopThread());
            received_value = event.value;
            event_received.Notify();
        });

        // 4. Fire an event. The callback should be active.
        callbackSource.FireEvent({100, std::this_thread::get_id()});
        ASSERT_TRUE(event_received.WaitForNotificationWithTimeout(absl::Seconds(2)));
        EXPECT_EQ(received_value, 100);
        EXPECT_EQ(callbackSource.CallbackCount(), 1);
    }  // <-- `scoped_handle` is destroyed here.

    // 5. The callback should now be unregistered.
    EXPECT_EQ(callbackSource.CallbackCount(), 0);

    // 6. Fire the event again. The notification should not be triggered.
    absl::Notification event_received_again;
    callbackSource.FireEvent({200, std::this_thread::get_id()});
    EXPECT_FALSE(event_received_again.WaitForNotificationWithTimeout(absl::Milliseconds(50)));
    EXPECT_EQ(received_value, 100);  // The value should not have changed.

    // 7. Clean up.
    ASSERT_THAT(eventLoop->ShutdownAndWait(absl::Milliseconds(500)), absl_testing::IsOk());
    loopThread.join();
}

}  // namespace goldfish::async::tests
