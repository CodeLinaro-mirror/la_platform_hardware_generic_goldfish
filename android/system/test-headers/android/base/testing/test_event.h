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

#pragma once

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

/**
 * @brief Helper for multithreaded tests to wait for an event to occur before
 * continuing test execution.
 *
 * Usage:
 * @code
 * TestEvent event;
 * setCallback([&event]() {
 *     event.Signal();
 * });
 *
 * asyncCallCallback();
 * event.Wait();
 * @endcode
 *
 * By default, the timeout is 1 second but it can be changed by overriding
 * the default parameter of Wait().
 *
 * TestEvent is counted, so calling Signal() more than once will result in
 * multiple Wait() events being triggered.  Call Reset() to reset the current
 * count.
 */
class TestEvent {
  public:
    static constexpr absl::Duration kDefaultTimeout = absl::Seconds(1);

    TestEvent() = default;
    TestEvent(const TestEvent& other) = delete;
    TestEvent& operator=(const TestEvent& other) = delete;

    /**
     * @brief Signals that the event has occurred.
     *
     * Increments the internal signal count and wakes up any waiting threads.
     */
    void Signal() {
        absl::MutexLock lock(&mutex_);
        ++signal_count_;
    }

    /**
     * @brief Checks if the event has been signaled.
     *
     * @return true if the signal count is greater than 0.
     */
    bool IsSignaled() const {
        absl::MutexLock lock(&mutex_);
        return signal_count_ > 0;
    }

    /**
     * @brief Resets the event state.
     *
     * Sets the signal count back to 0.
     */
    void Reset() {
        absl::MutexLock lock(&mutex_);
        signal_count_ = 0;
    }

    /**
     * @brief Waits for the event to be signaled.
     *
     * Blocks until the event is signaled or the timeout expires.
     * If the timeout expires, a GTest failure is generated.
     * If successful, the signal count is decremented.
     *
     * @param timeout The maximum duration to wait. Defaults to kDefaultTimeout.
     */
    void Wait(absl::Duration timeout = kDefaultTimeout) {
        absl::MutexLock lock(&mutex_);
        if (!mutex_.AwaitWithTimeout(
                    absl::Condition(
                            +[](size_t* count) { return *count > 0; }, &signal_count_),
                    timeout)) {
            FAIL() << "TestEvent::Wait() timed out.";
        }
        ASSERT_GT(signal_count_, 0);
        --signal_count_;
    }

  private:
    mutable absl::Mutex mutex_;
    size_t signal_count_ ABSL_GUARDED_BY(mutex_) = 0;
};
