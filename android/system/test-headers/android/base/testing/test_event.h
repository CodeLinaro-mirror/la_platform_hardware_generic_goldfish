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

// Helper for multithreaded tests to wait for an event to occur before
// continuing test execution. Usage:
//
// TestEvent event;
// setCallback([&event]() {
//     event.signal();
// });
//
// asyncCallCallback();
// event.wait();
//
// By default, the timeout is 10 seconds but it can be changed by overriding
// the default parameter of wait().
//
// TestEvent is counted, so calling signal() more than once will result in
// multiple wait() events being triggered.  Call reset() to reset the current
// count.

class TestEvent {
  public:
    static constexpr int64_t kDefaultTimeoutMs = 10000;  // 10 seconds.

    TestEvent() = default;
    TestEvent(const TestEvent& other) = delete;
    TestEvent& operator=(const TestEvent& other) = delete;

    void signal() {
        absl::MutexLock lock(&mutex_);
        ++signal_count_;
    }

    bool isSignaled() {
        absl::MutexLock lock(&mutex_);
        return signal_count_ > 0;
    }

    void reset() {
        absl::MutexLock lock(&mutex_);
        signal_count_ = 0;
    }

    void wait(int64_t timeoutMs = kDefaultTimeoutMs) {
        absl::MutexLock lock(&mutex_);
        if (!mutex_.AwaitWithTimeout(
                    absl::Condition(
                            +[](size_t* count) { return *count > 0; }, &signal_count_),
                    absl::Milliseconds(timeoutMs))) {
            FAIL() << "TestEvent::wait() timed out.";
        }
        ASSERT_GT(signal_count_, 0);
        --signal_count_;
    }

  private:
    absl::Mutex mutex_;
    size_t signal_count_ ABSL_GUARDED_BY(mutex_) = 0;
};
