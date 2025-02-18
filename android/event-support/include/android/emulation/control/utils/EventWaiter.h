// Copyright (C) 2020 The Android Open Source Project
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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "android/emulation/control/utils/EventSupport.h"

namespace android {
namespace emulation {
namespace control {

using Callback = void (*)(void* opaque);
using RegisterCallback = void (*)(Callback frameAvailable, void* opaque);
using RemoveCallback = void (*)(void* opaque);

// An EventWaiter can be used to block and wait for events that are coming from
// qemu. The QEMU engine that produces events must provide the following C
// interface:
//
// - A RegisterCallback function, which can be used to register a Callback. The
//   Callback should be invoked when a new event is available. Registration is
//   identified by the opaque pointer.
// - A RemoveCallback function, that can be used to unregister a Callback
//   identified by the opaque pointer. No events should be delivered after the
//   return of this function. You will see crashes if you do.
//
// Typical usage would be something like:
//
// int cnt = 0;
// EventWaiter frameEvent(&gpu_register_shared_memory_callback,
// &gpu_unregister_shared_memory_callback);
//
// while(cnt < 10) {
//    if (frameEvent.next(std::chrono::seconds(1)) > 0)
//      handleEvent(cnt++);
// }
//
// If you wish to use the EventWaiter without the registration mechanism
// you can use the default constructor. Call the "newEvent" method to
// notify listeners of a newEvent.
class EventWaiter {
  public:
    EventWaiter() = default;
    EventWaiter(RegisterCallback add, RemoveCallback remove);
    ~EventWaiter();

    // Block and wait at most timeout milliseconds until the next event
    // arrives. returns the number of missed events. Note, that this is
    // the least number of events you missed, you might have missed more.
    //
    // |afterEvent| Wait until we have received event number x+1.
    // |timeout|  Maximimum time to wait in milliseconds.
    uint64_t next(uint64_t afterEvent, std::chrono::milliseconds timeout);

    // Block and wait at most timeout milliseconds until the next event
    // arrives. Returns the number of missed events.
    //
    // This convenience method is not thread safe, and might not accurately
    // report events if multiple threads are using the same waiter.
    //
    // |timeout|  Maximimum time to wait in milliseconds.
    uint64_t next(std::chrono::milliseconds timeout);

    // The number of events received by the waiter. This is a monotonically
    // increasing number.
    uint64_t current() const;

    // Call this when a new event has arrived.
    void newEvent();

  private:
    static void callbackForwarder(void* opaque);

    std::mutex mStreamLock;
    std::condition_variable mCv;
    uint64_t mEventCounter{0};
    std::atomic<uint64_t> mLastEvent{0};
    RemoveCallback mRemove{nullptr};
};

/**
 * @brief A generic event waiter class that allows waiting for events with sequence numbers.
 *
 * This class provides a mechanism to wait for new events of type T arriving through an
 * EventChangeSupport, ensuring that events are processed in sequence.  It manages an internal event
 * sequence counter.
 *
 * @tparam T The type of the events to be handled.
 */
template <typename T>
class GenericEventWaiter {
  private:
    class InnerEventListener : public GenericEventHandler<T> {
      public:
        InnerEventListener(GenericEventWaiter<T>* waiter, EventChangeSupport<T>* listener)
            : GenericEventHandler<T>(listener), mWaiter(waiter) {}
        ~InnerEventListener() {}
        void eventArrived(const T event) override { mWaiter->onEventArrived(); }

      private:
        GenericEventWaiter<T>* mWaiter;
    };

  public:
    /**
     * @brief Constructs a GenericEventWaiter.
     *
     * @param listener A pointer to the EventChangeSupport that this waiter will listen to.
     */
    GenericEventWaiter(EventChangeSupport<T>* listener) : mInnerListener(this, listener) {}

    ~GenericEventWaiter() = default;

    /**
     * @brief Waits for a new event with a sequence number greater than the given one.
     *
     * This method blocks until either a new event with a higher sequence number than
     * |lastSequenceNumber| arrives or the specified timeout is reached.
     *
     * @param timeout The maximum duration to wait for a new event.
     * @param lastSequenceNumber The sequence number of the last processed event.  The waiter
     *        will only return true if a new event with a greater sequence number arrives.
     * @return True if a new event with a larger sequence number arrived, false if the timeout
     *         was reached.
     */
    bool waitForNextEvent(absl::Duration timeout, uint64_t lastSequenceNumber) const {
        auto nextEvent = [&]() {
            // Note: that the following holds:
            // mEventSequenceMutex.AssertHeld();
            return mEventSequence > lastSequenceNumber;
        };
        absl::MutexLock lock(&mEventSequenceMutex);
        mEventSequenceMutex.AwaitWithTimeout(absl::Condition(&nextEvent), timeout);
        return mEventSequence > lastSequenceNumber;
    }

    /**
     * @brief Waits for any new event to arrive.
     *
     * This method blocks until a new event arrives or the timeout is reached. It is equivalent
     * to calling `waitForNextEvent` with the current event sequence number.  This means the
     * method will return immediately if an event has already arrived *before* this method is
     * called, even if that event has already been processed by another thread.
     *
     * @note This method is not race-free. If an event arrives between retrieving the current
     * sequence number and calling `waitForNextEvent`, this call will miss that event and may
     * wait for the *next* event.  If precise event ordering is essential or if the event
     * source could fire events very rapidly, it's recommended to use the overload
     * `waitForNextEvent(timeout, lastSequenceNumber)` and manage the sequence numbers
     * explicitly.
     *
     * @param timeout The maximum duration to wait for a new event.
     * @return True if a new event arrived, false if the timeout was reached.
     */
    bool waitForNextEvent(absl::Duration timeout) const {
        return waitForNextEvent(timeout, mEventSequence);
    }

    /**
     * @brief Gets the current event sequence number.
     *
     * @return The current event sequence number.
     */
    uint64_t getEventSequence() const {
        absl::MutexLock lock(&mEventSequenceMutex);
        return mEventSequence;
    }

  private:
    void onEventArrived() {
        absl::MutexLock lock(&mEventSequenceMutex);
        mEventSequence++;
    }

    /// The current event sequence number. Guarded by |mEventSequenceMutex|.
    uint64_t mEventSequence ABSL_GUARDED_BY(mEventSequenceMutex) = 0;
    /// Mutex for protecting access to |mEventSequence|.
    mutable absl::Mutex mEventSequenceMutex;
    InnerEventListener mInnerListener;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
