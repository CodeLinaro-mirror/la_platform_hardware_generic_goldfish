// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may not use this file except in compliance with the License.
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

#include "absl/time/time.h"

#include "goldfish/async/event_loop.h"

namespace goldfish::async::testing {

/**
 * @brief An abstract interface for an EventLoop that can be manually controlled
 * in tests.
 *
 * This class provides a clear separation between running immediately-posted
 * tasks and advancing time to run scheduled tasks.
 */
class TestEventLoop : public EventLoop {
  public:
    using EventLoop::EventLoop;
    ~TestEventLoop() override = default;
    /**
     * @brief Factory function to create a concrete instance of the
     * TestEventLoop.
     * @return A unique_ptr to a new TestEventLoop instance.
     */
    static std::unique_ptr<TestEventLoop> Create(std::string name = "TestLoop");

    /**
     * @brief Runs all immediately pending tasks until the queue is empty.
     *
     * This method does not advance the clock and only executes tasks posted
     * via post() without a delay.
     */
    virtual void RunAll() = 0;

    /**
     * @brief Runs at most one immediately pending task.
     * @return True if a task was executed, false if the queue was empty.
     */
    virtual bool RunOne() = 0;

    /**
     * @brief Runs up to a specified number of immediately pending tasks.
     * @param count The maximum number of tasks to run.
     * @return The number of tasks that were actually executed.
     */
    virtual size_t RunMany(size_t count) = 0;

    /**
     * @brief Number of scheduled tasks
     * @return The number of scheduled tasks.
     */
    virtual size_t TaskCount() const = 0;
    /**
     * @brief Advances the loop's internal clock by a specified duration.
     *
     * After advancing the clock, this method will run all scheduled tasks
     * that became due during that time interval. It does not run any
     * immediately pending tasks from the separate queue.
     * @param duration The amount of time to advance the clock.
     */
    virtual void AdvanceClock(absl::Duration duration) = 0;
};
}  // namespace goldfish::async::testing