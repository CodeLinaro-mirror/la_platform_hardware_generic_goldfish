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
#pragma once

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "goldfish/eventing/event_sources.h"
#include "goldfish/eventing/policies/pointer_handlers.h"

namespace android::base::eventing {

/**
 * @brief A generic event waiter that allows a thread to block until an event
 * arrives from one or more EventSource instances.
 *
 * This class provides a mechanism to wait for new events arriving from multiple
 * EventSource instances of potentially different types. It manages a single
 * internal event sequence counter. Any event arriving on any of the listened-to
 * sources will unblock the wait and increment the counter.
 *
 * The waiter is robust and can handle different types of event sources,
 * including those that require raw pointer listeners and "safe" sources that
 * require `std::weak_ptr` listeners.
 *
 * ### Simple Usage
 * For event sources with an unambiguous event type.
 *
 * @code
 *   // Given:
 *   //   EventSource<MyEvent> myEventSource;
 *   //   SensorObserver sensor; // (implements EventSource<SensorData>)
 *
 *   MultiEventSourceWaiter waiter;
 *   waiter.listen(&myEventSource);
 *   waiter.listen(&sensor);
 *
 *   uint64_t lastEvent = waiter.getEventSequence();
 *   if (waiter.waitForNextEvent(absl::Seconds(1), lastEvent)) {
 *     // An event from one of the sources arrived.
 *   }
 * @endcode
 *
 * ### Advanced Usage (Resolving Ambiguity)
 * If a class inherits from `EventSource` multiple times, you must explicitly
 * specify which event source to listen to.
 *
 * @code
 *   // Given:
 *   //   class MyDisplay : public SafeEventSource<FrameInfo>,
 *   //                     public SafeEventSource<ResizeEvent> {};
 *   //   MyDisplay display;
 *
 *   MultiEventSourceWaiter waiter;
 *
 *   // ERROR: This is ambiguous. Does listen mean FrameInfo or ResizeEvent?
 *   // waiter.listen(&display);
 *
 *   // CORRECT: Explicitly specify the base class to listen to.
 *   waiter.listen<SafeEventSource<FrameInfo>>(
 *       static_cast<SafeEventSource<FrameInfo>*>(&display));
 *
 *   uint64_t lastEvent = waiter.getEventSequence();
 *   waiter.waitForNextEvent(absl::Seconds(1), lastEvent);
 * @endcode
 */
class MultiEventSourceWaiter {
  private:
    // The IListenerHandle serves as a type-erased base class. This allows us to
    // store shared_ptr instances of different InnerEventListener specializations
    // in a single std::vector, effectively erasing the template parameters of
    // the inner listeners.
    class IListenerHandle {
      public:
        virtual ~IListenerHandle() = default;
    };

    // The InnerEventListener is the core of the waiter. An instance of this
    // class is created for each EventSource that the waiter listens to. Its
    // primary responsibilities are:
    //
    // 1.  **Subscription:** It subscribes to a specific EventSource in its
    //     constructor and unsubscribes in its destructor, ensuring RAII-style
    //     lifetime management.
    // 2.  **Pointer Handling:** It uses `if constexpr` to automatically detect
    //     whether the target EventSource requires a raw pointer listener (this)
    //     or a `std::weak_ptr` listener (by passing a convertible `shared_ptr`).
    // 3.  **Event Forwarding:** When its `eventArrived` method is called by the
    //     source, it forwards the notification to the parent
    //     MultiEventSourceWaiter's `onEventArrived` method, which increments
    //     the sequence counter and wakes up any waiting threads.
    template <typename EventSourceType>
    class InnerEventListener
            : public IListenerHandle,
              public EventListener<typename EventSourceType::EventType>,
              public std::enable_shared_from_this<InnerEventListener<EventSourceType>> {
      public:
        using EventType = typename EventSourceType::EventType;

        InnerEventListener(MultiEventSourceWaiter* waiter, EventSourceType* source)
                : waiter_(waiter), source_(source) {}

        // Subscribes to the event source, handling both raw and weak_ptr listeners.
        void Subscribe() {
            using Ptr = typename EventSourceType::Ptr;
            if constexpr (is_weak_ptr_v<Ptr>) {
                // The source is a "safe" source and expects a weak_ptr. We pass
                // our shared_ptr, which will be implicitly converted.
                self_ = this->shared_from_this();
                source_->AddListener(self_);
            } else {
                // The source expects a raw pointer.
                source_->AddListener(this);
            }
        }

        // Unsubscribes from the event source upon destruction.
        ~InnerEventListener() override {
            using Ptr = typename EventSourceType::Ptr;
            if constexpr (is_weak_ptr_v<Ptr>) {
                if (auto self = self_.lock()) {
                    source_->RemoveListener(self);
                }
            } else {
                source_->RemoveListener(this);
            }
        }

        void EventArrived(const EventType& /*event*/) override { waiter_->OnEventArrived(); }

      private:
        MultiEventSourceWaiter* waiter_;
        EventSourceType* source_;
        std::weak_ptr<InnerEventListener<EventSourceType>> self_;
    };

    // Stores a handle to each active inner listener, keeping them alive.
    std::vector<std::shared_ptr<IListenerHandle>> listeners_;

  public:
    MultiEventSourceWaiter() = default;

    /**
     * @brief Listens for events from a given EventSource.
     *
     * For sources with multiple EventSource base classes, this template
     * parameter must be used to explicitly specify the unambiguous base type
     * to listen to.
     *
     * @tparam EventSourceType The unambiguous type of the event source.
     * @param source A pointer to the event source.
     */
    template <typename EventSourceType>
    void Listen(EventSourceType* source) {
        auto listener = std::make_shared<InnerEventListener<EventSourceType>>(this, source);
        listener->Subscribe();
        listeners_.push_back(listener);
    }

    /**
     * @brief Waits for a new event with a sequence number greater than the
     * given one.
     */
    bool WaitForNextEvent(absl::Duration timeout, uint64_t last_sequence_number) const {
        auto next_event = [&]() ABSL_SHARED_LOCKS_REQUIRED(event_sequence_mutex_) {
            return event_sequence_ > last_sequence_number;
        };
        const absl::MutexLock lock(&event_sequence_mutex_);
        event_sequence_mutex_.AwaitWithTimeout(absl::Condition(&next_event), timeout);
        return event_sequence_ > last_sequence_number;
    }

    /**
     * @brief Waits for any new event to arrive.
     */
    bool WaitForNextEvent(absl::Duration timeout) const {
        return WaitForNextEvent(timeout, GetEventSequence());
    }

    /**
     * @brief Gets the current event sequence number.
     */
    uint64_t GetEventSequence() const {
        const absl::MutexLock lock(&event_sequence_mutex_);
        return event_sequence_;
    }

  private:
    void OnEventArrived() {
        const absl::MutexLock lock(&event_sequence_mutex_);
        event_sequence_++;
    }

    mutable absl::Mutex event_sequence_mutex_;
    uint64_t event_sequence_ ABSL_GUARDED_BY(event_sequence_mutex_) = 0;
};

}  // namespace android::base::eventing