
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
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"

namespace goldfish::async {
/**
 * @class ScopedTimer
 * @brief A generic, thread-safe RAII wrapper for an asynchronous timer.
 *
 * This class ensures that a timer is reliably canceled when the object goes
 * out of scope. It prevents "dangling" timers that could continue to fire
 * after they're no longer needed, using a mutex to ensure thread-safe
 * access and cancellation.
 */
class ScopedTimer final : public async::EventLoop::Timer {
  public:
    explicit ScopedTimer(std::shared_ptr<async::EventLoop::Timer> timer)
            : mTimer(std::move(timer)) {}
    ~ScopedTimer() override {
        cancel();
    }

    void cancel() override {
        absl::MutexLock lock(&mTimerMutex);
        if (mTimer) {
            mTimer->cancel();
            mTimer.reset();
        }
    }

    void schedule(std::chrono::milliseconds new_delay,
                             std::chrono::milliseconds new_interval) override {
        absl::MutexLock lock(&mTimerMutex);
        if (mTimer) {
            mTimer->schedule(new_delay, new_interval);
        }
    }

  private:
    absl::Mutex mTimerMutex;
    std::shared_ptr<async::EventLoop::Timer> mTimer ABSL_GUARDED_BY(mTimerMutex);
};
}  // namespace goldfish::async