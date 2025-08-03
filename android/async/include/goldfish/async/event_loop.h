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
#include <functional>
#include <future>

#include "absl/log/log.h"
#include "absl/status/status.h"

namespace goldfish::async {
/**
 * @brief An abstract interface for an event loop.
 *
 * This allows application code to depend on the concept of an event loop
 * without being tied to a specific implementation like libuv or asio.
 */
class EventLoop {
  public:
    /**
     * @brief A unit of work to be executed by the EventLoop.
     */
    using Task = std::function<void()>;

    virtual ~EventLoop() = default;

    /**
     * @brief Runs the event loop, blocking until stop() is called.
     */
    virtual void run() = 0;

    /**
     * @brief Stops a running event loop. This method is thread-safe.
     */
    virtual void stop() = 0;

    /**
     * @brief Checks if the current thread is the one running this event loop.
     * @return true if the caller is on the event loop's thread, false otherwise.
     */
    virtual bool isOnLoopThread() = 0;

    /**
     * @brief Posts a task to be invoked on the EventLoop's thread.
     *
     * This method is thread-safe and can be called from any thread to
     * delegate work to the event loop.
     *
     * @param task The function to be executed.
     * @return absl::OkStatus() if the task was successfully posted, or an
     * error status on immediate failure.
     */
    virtual absl::Status post(Task task) = 0;

    /**
     * @brief Posts a task to the event loop and blocks the calling thread
     * until the task is complete.
     *
     * @tparam F The type of the callable task.
     * @param task The task to execute.
     * @return The value returned by the task.
     *
     * @warning This method MUST NOT be called from the event loop's own
     * thread, as it will cause an immediate deadlock.
     */
    template <typename F>
    auto postAndWait(F&& task) -> decltype(task()) {
        if (isOnLoopThread()) {
            LOG(FATAL) << "postAndWait cannot be called from the event loop.";
        }

        using ResultType = decltype(task());
        auto promise = std::make_shared<std::promise<ResultType>>();
        auto future = promise->get_future();

        post([promise, task = std::forward<F>(task)]() mutable {
            // Handle tasks that return void vs. a value
            if constexpr (std::is_same_v<ResultType, void>) {
                task();
                promise->set_value();
            } else {
                promise->set_value(task());
            }
        });

        return future.get();
    }

    // Implementation specific loop.
    virtual void* getRawLoop() const = 0;
};

}  // namespace goldfish::async