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
#include "goldfish/eventing/event_sources.h"

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "goldfish/eventing/multi_event_source_waiter.h"

namespace {

using android::base::eventing::CallbackEventSource;
using android::base::eventing::FastEventSource;
using android::base::eventing::HighContentionEventSource;
using android::base::eventing::MakeScopedCallback;
using android::base::eventing::MultiEventSourceWaiter;
using android::base::eventing::SafeEventSource;
using android::base::eventing::ScopedEventCallback;

// A simple event type for testing.
struct TestEvent {
    int value;
    bool operator==(const TestEvent& other) const { return value == other.value; }
};

// A second event type for testing ambiguous sources.
struct TestEvent2 {
    std::string value;
};

// A mock listener that records received events in a thread-safe manner.
template <class T>
class MockEventListener : public android::base::eventing::EventListener<T> {
  public:
    void EventArrived(const T& event) override {
        const std::lock_guard<std::mutex> lock(mutex);
        received_events.push_back(event);
    }

    std::vector<T> received_events;
    std::mutex mutex;
};

// A typed test suite to verify the common interface of all raw-pointer-based
// event sources. This reduces code duplication.
template <typename T>
class EventSourceTest : public ::testing::Test {};

using RawPointerSources =
        ::testing::Types<FastEventSource<TestEvent>, HighContentionEventSource<TestEvent>>;

TYPED_TEST_SUITE(EventSourceTest, RawPointerSources);

TYPED_TEST(EventSourceTest, AddAndFireEvent) {
    TypeParam source;
    MockEventListener<TestEvent> listener;
    const TestEvent event{42};

    source.AddListener(&listener);
    ASSERT_EQ(source.Size(), 1);

    source.FireEvent(event);
    ASSERT_EQ(listener.received_events.size(), 1);
    EXPECT_EQ(listener.received_events[0], event);
}

TYPED_TEST(EventSourceTest, RemoveListener) {
    TypeParam source;
    MockEventListener<TestEvent> listener;
    const TestEvent event{42};

    source.AddListener(&listener);
    source.RemoveListener(&listener);
    ASSERT_EQ(source.Size(), 0);

    source.FireEvent(event);
    EXPECT_TRUE(listener.received_events.empty());
}

TYPED_TEST(EventSourceTest, FireToMultipleListeners) {
    TypeParam source;
    MockEventListener<TestEvent> listener1;
    MockEventListener<TestEvent> listener2;
    const TestEvent event{42};

    source.AddListener(&listener1);
    source.AddListener(&listener2);
    ASSERT_EQ(source.Size(), 2);

    source.FireEvent(event);
    ASSERT_EQ(listener1.received_events.size(), 1);
    EXPECT_EQ(listener1.received_events[0], event);
    ASSERT_EQ(listener2.received_events.size(), 1);
    EXPECT_EQ(listener2.received_events[0], event);
}

// A specific test to verify the basic thread safety of HighContentionEventSource.
TEST(HighContentionEventSourceTest, BasicThreadSafety) {
    HighContentionEventSource<TestEvent> source;
    MockEventListener<TestEvent> listener;
    source.AddListener(&listener);

    constexpr int kNumThreads = 4;
    constexpr int kEventsPerThread = 100;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&source, i]() {
            for (int j = 0; j < kEventsPerThread; ++j) {
                source.FireEvent({(i * 1000) + j});
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(listener.received_events.size(), kNumThreads * kEventsPerThread);
}

// Tests for SafeEventSource, focusing on its weak_ptr behavior.
TEST(SafeEventSourceTest, AddAndFireEvent) {
    SafeEventSource<TestEvent> source;
    auto listener = std::make_shared<MockEventListener<TestEvent>>();
    const TestEvent event{42};

    source.AddListener(listener);
    ASSERT_EQ(source.Size(), 1);

    source.FireEvent(event);
    ASSERT_EQ(listener->received_events.size(), 1);
    EXPECT_EQ(listener->received_events[0], event);
}

TEST(SafeEventSourceTest, HandlesDestroyedListenerGracefully) {
    SafeEventSource<TestEvent> source;
    auto listener = std::make_shared<MockEventListener<TestEvent>>();
    const TestEvent event{42};

    source.AddListener(listener);
    ASSERT_EQ(source.Size(), 1);

    // Destroy the listener object.
    listener.reset();

    // The source should correctly report that there are no more live listeners.
    ASSERT_EQ(source.Size(), 0);

    // Firing the event should not crash and should clean up the expired pointer
    // internally.
    source.FireEvent(event);
    ASSERT_EQ(source.Size(), 0);
}

// Tests for CallbackEventSource, focusing on the WithCallbacks API.
TEST(CallbackEventSourceTest, AddCallbackAndFire) {
    CallbackEventSource<TestEvent> source;
    std::vector<TestEvent> received_events;
    const TestEvent event{42};

    auto handle =
            MakeScopedCallback(source, [&](const TestEvent& e) { received_events.push_back(e); });

    ASSERT_EQ(source.Size(), 1);

    source.FireEvent(event);
    ASSERT_EQ(received_events.size(), 1);
    EXPECT_EQ(received_events[0], event);
}

TEST(CallbackEventSourceTest, HandleUnsubscribesOnDestruction) {
    CallbackEventSource<TestEvent> source;
    std::vector<TestEvent> received_events;
    const TestEvent event{42};

    {
        auto handle = MakeScopedCallback(source,
                                         [&](const TestEvent& e) { received_events.push_back(e); });
        ASSERT_EQ(source.Size(), 1);
    }  // The RAII handle goes out of scope here, unsubscribing the callback.

    ASSERT_EQ(source.Size(), 0);

    source.FireEvent(event);
    EXPECT_TRUE(received_events.empty());
}

TEST(CallbackEventSourceTest, CallbackIdStartsAtOneAndIncrements) {
    CallbackEventSource<TestEvent> source;
    EXPECT_EQ(CallbackEventSource<TestEvent>::kInvalidCallbackId, 0);

    auto id1 = source.AddCallback([](const TestEvent&) {});
    EXPECT_EQ(id1, 1);
    EXPECT_NE(id1, CallbackEventSource<TestEvent>::kInvalidCallbackId);

    auto id2 = source.AddCallback([](const TestEvent&) {});
    EXPECT_EQ(id2, 2);
    EXPECT_NE(id2, CallbackEventSource<TestEvent>::kInvalidCallbackId);

    EXPECT_EQ(source.CallbackCount(), 2);
    source.RemoveCallback(id1);
    EXPECT_EQ(source.CallbackCount(), 1);
    source.RemoveCallback(id2);
    EXPECT_EQ(source.CallbackCount(), 0);
}

TEST(CallbackEventSourceTest, RemoveInvalidCallbackIdIsSafe) {
    CallbackEventSource<TestEvent> source;
    // Removing invalid ID when empty
    source.RemoveCallback(CallbackEventSource<TestEvent>::kInvalidCallbackId);
    EXPECT_EQ(source.CallbackCount(), 0);

    auto id = source.AddCallback([](const TestEvent&) {});
    EXPECT_EQ(source.CallbackCount(), 1);

    // Removing invalid ID does not remove registered callback
    source.RemoveCallback(CallbackEventSource<TestEvent>::kInvalidCallbackId);
    EXPECT_EQ(source.CallbackCount(), 1);

    source.RemoveCallback(id);
    EXPECT_EQ(source.CallbackCount(), 0);
}

TEST(CallbackEventSourceTest, ScopedEventCallbackMoveInvalidatesSourceHandle) {
    CallbackEventSource<TestEvent> source;
    int received = 0;

    {
        auto h1 = MakeScopedCallback(source, [&](const TestEvent&) { received++; });
        EXPECT_EQ(source.CallbackCount(), 1);
        EXPECT_EQ(h1->GetId(), 1);

        {
            auto h2 = std::move(h1);
            EXPECT_EQ(source.CallbackCount(), 1);
            EXPECT_EQ(h2->GetId(), 1);

            source.FireEvent({1});
            EXPECT_EQ(received, 1);
        }  // h2 destroyed, unregisters callback

        EXPECT_EQ(source.CallbackCount(), 0);
    }  // h1 destroyed, moved-from ID is kInvalidId, should be safe no-op

    EXPECT_EQ(source.CallbackCount(), 0);
}

TEST(CallbackEventSourceTest, ScopedEventCallbackDefaultConstructor) {
    using ScopedCallback = ScopedEventCallback<CallbackEventSource<TestEvent>, TestEvent>;
    ScopedCallback handle;
    EXPECT_EQ(handle.GetId(), ScopedCallback::kInvalidId);
    // Destructor of default-constructed handle is a safe no-op.
}

TEST(CallbackEventSourceTest, ScopedEventCallbackValueMoveSemantics) {
    using ScopedCallback = ScopedEventCallback<CallbackEventSource<TestEvent>, TestEvent>;
    CallbackEventSource<TestEvent> source;
    int received = 0;

    {
        ScopedCallback cb1(source, [&](const TestEvent&) { received++; });
        EXPECT_EQ(source.CallbackCount(), 1);
        EXPECT_EQ(cb1.GetId(), 1);

        {
            auto cb2 = std::move(cb1);
            EXPECT_EQ(cb1.GetId(), ScopedCallback::kInvalidId);
            EXPECT_EQ(cb2.GetId(), 1);
            EXPECT_EQ(source.CallbackCount(), 1);

            source.FireEvent({42});
            EXPECT_EQ(received, 1);
        }  // cb2 destroyed, unregisters callback

        EXPECT_EQ(source.CallbackCount(), 0);
    }  // cb1 destroyed as moved-from, safe no-op

    EXPECT_EQ(source.CallbackCount(), 0);
}

TEST(CallbackEventSourceTest, ScopedEventCallbackValueMoveAssignment) {
    using ScopedCallback = ScopedEventCallback<CallbackEventSource<TestEvent>, TestEvent>;
    CallbackEventSource<TestEvent> source;
    int count1 = 0;
    int count2 = 0;

    ScopedCallback cb1(source, [&](const TestEvent&) { count1++; });
    ScopedCallback cb2(source, [&](const TestEvent&) { count2++; });

    EXPECT_EQ(source.CallbackCount(), 2);

    // Move-assigning cb2 into cb1 cleans up cb1's callback and assumes cb2's callback.
    cb1 = std::move(cb2);
    EXPECT_EQ(source.CallbackCount(), 1);
    EXPECT_EQ(cb2.GetId(), ScopedCallback::kInvalidId);

    source.FireEvent({100});
    EXPECT_EQ(count1, 0);
    EXPECT_EQ(count2, 1);
}

TEST(CallbackEventSourceTest, ScopedEventCallbackVoidSpecialization) {
    struct MockVoidSystem {
        size_t AddCallback(std::function<void()> cb) {
            callback = std::move(cb);
            return ++next_id;
        }
        void RemoveCallback(size_t id) {
            if (id != 0) {
                removed_id = id;
                callback = nullptr;
            }
        }
        std::function<void()> callback;
        size_t next_id = 0;
        size_t removed_id = 0;
    };

    MockVoidSystem system;
    int calls = 0;

    using ScopedCallback = ScopedEventCallback<MockVoidSystem, void>;
    ScopedCallback empty_cb;
    EXPECT_EQ(empty_cb.GetId(), ScopedCallback::kInvalidId);

    {
        ScopedCallback cb1(system, [&]() { calls++; });
        EXPECT_EQ(cb1.GetId(), 1);

        empty_cb = std::move(cb1);
        EXPECT_EQ(cb1.GetId(), ScopedCallback::kInvalidId);
        EXPECT_EQ(empty_cb.GetId(), 1);
        EXPECT_EQ(system.removed_id, 0);

        system.callback();
        EXPECT_EQ(calls, 1);
    }
    // cb1 destroyed (moved-from, safe no-op)
    EXPECT_EQ(system.removed_id, 0);

    empty_cb = {};  // Cleans up callback 1
    EXPECT_EQ(system.removed_id, 1);
    EXPECT_EQ(empty_cb.GetId(), ScopedCallback::kInvalidId);
}

TEST(CallbackEventSourceTest, ScopedEventCallbackTriggersLifecycleInterception) {
    class InterceptingEventSystem {
      public:
        using CallbackId = size_t;

        CallbackId AddCallback(std::function<void(const TestEvent&)> cb) {
            auto id = source_.AddCallback(std::move(cb));
            if (active_count++ == 0) {
                on_subscribe_count++;
            }
            return id;
        }

        void RemoveCallback(CallbackId id) {
            if (id != 0) {
                source_.RemoveCallback(id);
                if (--active_count == 0) {
                    on_unsubscribe_count++;
                }
            }
        }

        void FireEvent(const TestEvent& event) { source_.FireEvent(event); }

        int on_subscribe_count = 0;
        int on_unsubscribe_count = 0;
        int active_count = 0;

      private:
        CallbackEventSource<TestEvent> source_;
    };

    InterceptingEventSystem system;
    EXPECT_EQ(system.active_count, 0);
    EXPECT_EQ(system.on_subscribe_count, 0);
    EXPECT_EQ(system.on_unsubscribe_count, 0);

    using ScopedCallback = ScopedEventCallback<InterceptingEventSystem, TestEvent>;

    int received1 = 0;
    int received2 = 0;

    {
        ScopedCallback cb1(system, [&](const TestEvent&) { received1++; });
        EXPECT_EQ(system.active_count, 1);
        EXPECT_EQ(system.on_subscribe_count, 1);
        EXPECT_EQ(system.on_unsubscribe_count, 0);

        {
            ScopedCallback cb2(system, [&](const TestEvent&) { received2++; });
            EXPECT_EQ(system.active_count, 2);
            EXPECT_EQ(system.on_subscribe_count, 1);  // Only triggered on 0 -> 1
            EXPECT_EQ(system.on_unsubscribe_count, 0);

            system.FireEvent({42});
            EXPECT_EQ(received1, 1);
            EXPECT_EQ(received2, 1);
        }  // cb2 destroyed -> active_count 2 -> 1, no unsubscribe hook yet
        EXPECT_EQ(system.active_count, 1);
        EXPECT_EQ(system.on_subscribe_count, 1);
        EXPECT_EQ(system.on_unsubscribe_count, 0);

        // Reassigning cb1 to empty handle cleans up cb1 -> active_count 1 -> 0, triggers
        // unsubscribe
        cb1 = {};
        EXPECT_EQ(system.active_count, 0);
        EXPECT_EQ(system.on_subscribe_count, 1);
        EXPECT_EQ(system.on_unsubscribe_count, 1);
    }

    // Exiting scope with already-disarmed cb1 does not double-unsubscribe
    EXPECT_EQ(system.active_count, 0);
    EXPECT_EQ(system.on_subscribe_count, 1);
    EXPECT_EQ(system.on_unsubscribe_count, 1);
}

// --- Tests for MultiEventSourceWaiter ---

TEST(MultiEventSourceWaiterTest, WaiterTimesOut) {
    FastEventSource<TestEvent> source;
    MultiEventSourceWaiter waiter;
    waiter.Listen(&source);

    const uint64_t last_event = waiter.GetEventSequence();
    EXPECT_FALSE(waiter.WaitForNextEvent(absl::Milliseconds(1), last_event));
}

TEST(MultiEventSourceWaiterTest, WaiterUnblocksOnSingleSource) {
    FastEventSource<TestEvent> source;
    MultiEventSourceWaiter waiter;
    waiter.Listen(&source);

    const uint64_t last_event = waiter.GetEventSequence();
    source.FireEvent({123});

    EXPECT_TRUE(waiter.WaitForNextEvent(absl::Seconds(1), last_event));
    EXPECT_EQ(waiter.GetEventSequence(), last_event + 1);
}

TEST(MultiEventSourceWaiterTest, WaiterUnblocksOnMultipleSources) {
    FastEventSource<TestEvent> source1;
    SafeEventSource<TestEvent2> source2;
    MultiEventSourceWaiter waiter;
    waiter.Listen(&source1);
    waiter.Listen(&source2);

    uint64_t last_event = waiter.GetEventSequence();

    // Fire the first source
    source1.FireEvent({1});
    EXPECT_TRUE(waiter.WaitForNextEvent(absl::Seconds(1), last_event));
    EXPECT_EQ(waiter.GetEventSequence(), last_event + 1);

    // Fire the second source
    last_event = waiter.GetEventSequence();
    source2.FireEvent({"hello"});
    EXPECT_TRUE(waiter.WaitForNextEvent(absl::Seconds(1), last_event));
    EXPECT_EQ(waiter.GetEventSequence(), last_event + 1);
}

TEST(MultiEventSourceWaiterTest, EventFiredBeforeWait) {
    FastEventSource<TestEvent> source;
    MultiEventSourceWaiter waiter;
    waiter.Listen(&source);

    const uint64_t last_event = waiter.GetEventSequence();
    source.FireEvent({123});

    // Should return immediately since the sequence number has advanced.
    EXPECT_TRUE(waiter.WaitForNextEvent(absl::ZeroDuration(), last_event));
}

TEST(MultiEventSourceWaiterTest, CorrectlyUsesSequenceNumber) {
    FastEventSource<TestEvent> source;
    MultiEventSourceWaiter waiter;
    waiter.Listen(&source);

    const uint64_t seq1 = waiter.GetEventSequence();
    source.FireEvent({1});

    // Wait should succeed because seq is now > seq1.
    EXPECT_TRUE(waiter.WaitForNextEvent(absl::Seconds(1), seq1));

    const uint64_t seq2 = waiter.GetEventSequence();
    EXPECT_GT(seq2, seq1);

    // Wait should time out because seq is not > seq2.
    EXPECT_FALSE(waiter.WaitForNextEvent(absl::Milliseconds(1), seq2));
}

TEST(MultiEventSourceWaiterTest, HandlesMixedSourceTypes) {
    FastEventSource<TestEvent> fast_source;   // Requires raw pointer
    SafeEventSource<TestEvent2> safe_source;  // Requires weak_ptr
    MultiEventSourceWaiter waiter;

    waiter.Listen(&fast_source);
    waiter.Listen(&safe_source);

    uint64_t last_event = waiter.GetEventSequence();
    fast_source.FireEvent({1});
    EXPECT_TRUE(waiter.WaitForNextEvent(absl::Seconds(1), last_event));

    last_event = waiter.GetEventSequence();
    safe_source.FireEvent({"test"});
    EXPECT_TRUE(waiter.WaitForNextEvent(absl::Seconds(1), last_event));
}

// A mock class that inherits from EventSource twice to create ambiguity.
class AmbiguousSource : public FastEventSource<TestEvent>, public SafeEventSource<TestEvent2> {};

TEST(MultiEventSourceWaiterTest, HandlesAmbiguousSource) {
    AmbiguousSource source;
    MultiEventSourceWaiter waiter;

    // Must explicitly cast to the desired base class to resolve ambiguity.
    waiter.Listen<FastEventSource<TestEvent>>(static_cast<FastEventSource<TestEvent>*>(&source));
    waiter.Listen<SafeEventSource<TestEvent2>>(static_cast<SafeEventSource<TestEvent2>*>(&source));

    uint64_t last_event = waiter.GetEventSequence();
    source.FastEventSource<TestEvent>::FireEvent({1});
    EXPECT_TRUE(waiter.WaitForNextEvent(absl::Seconds(1), last_event));

    last_event = waiter.GetEventSequence();
    source.SafeEventSource<TestEvent2>::FireEvent({"test"});
    EXPECT_TRUE(waiter.WaitForNextEvent(absl::Seconds(1), last_event));
}

TEST(MultiEventSourceWaiterTest, UnsubscribesOnDestruction) {
    FastEventSource<TestEvent> source;
    {
        MultiEventSourceWaiter waiter;
        waiter.Listen(&source);
        ASSERT_EQ(source.Size(), 1);
    }  // Waiter is destroyed here, should unsubscribe.

    ASSERT_EQ(source.Size(), 0);
    // Firing should not crash.
    source.FireEvent({1});
}

}  // namespace
