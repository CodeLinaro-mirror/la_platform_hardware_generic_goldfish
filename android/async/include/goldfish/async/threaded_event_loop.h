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
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "absl/status/status.h"

#include "aemu/base/events/CallbackEventSupport.h"
#include "goldfish/async/event_loop.h"

namespace goldfish::async {

/**
 * @brief A decorator that runs an EventLoop implementation in a background
 * thread.
 *
 * This class takes ownership of an EventLoop (e.g., a LibuvEventLoop) and
 * manages its execution in a dedicated thread. The run() method starts the
 * thread, and the destructor ensures the loop is stopped and the thread is
 * cleanly joined.
 */
class ThreadedEventLoop : public EventLoop {
  public:
    /**
     * @brief Constructs a ThreadedEventLoop.
     * @param loop A unique_ptr to the underlying EventLoop implementation that
     * this class will manage and run.
     */
    explicit ThreadedEventLoop(std::unique_ptr<EventLoop> loop,
                               std::string name = "AEMU Event Thread");
    ~ThreadedEventLoop() override;

    // --- Prevent Copying ---
    ThreadedEventLoop(const ThreadedEventLoop&) = delete;
    ThreadedEventLoop& operator=(const ThreadedEventLoop&) = delete;

    // --- Prevent Moving ---
    ThreadedEventLoop(ThreadedEventLoop&& other) noexcept = delete;
    ThreadedEventLoop& operator=(ThreadedEventLoop&& other) noexcept = delete;

    /**
     * @brief Starts the background thread and begins executing the underlying
     * event loop's run() method within it. This method returns immediately.
     */
    absl::Status run() override;

    /**
     * @brief Stops the underlying event loop and waits for the background
     * thread to complete its execution. This is a blocking call.
     */
    void stop() override;

    std::future<absl::Status> shutdown(std::chrono::milliseconds timeout) override;

    /**
     * @brief Checks if the caller is on the background event loop thread.
     * @return Delegates the call to the underlying EventLoop.
     */
    bool isOnLoopThread() const override;

    /**
     * @brief Posts a task to the underlying event loop to be executed on its
     * background thread.
     * @param task The task to execute.
     * @return The status of the post operation.
     */
    absl::Status post(Task task) override;

    absl::Status post(Task task, std::chrono::milliseconds delay) override;

    std::shared_ptr<Timer> scheduleDelayed(Task task, std::chrono::milliseconds delay) override;

    std::shared_ptr<Timer> scheduleRepeating(Task task, std::chrono::milliseconds initial_delay,
                                             std::chrono::milliseconds interval) override;

    void* getRawLoop() const override { return mLoop->getRawLoop(); }

    // Timeout used when calling shutdown, the destructor will
    // wait at most this amount before terminating...
    // Note: that is usually not a good thing.
    static constexpr std::chrono::milliseconds getTimeout() {
        return std::chrono::milliseconds(500);
    }

  private:
    std::thread mRunner;
    std::unique_ptr<EventLoop> mLoop;
    std::string mLooperName;
    std::unique_ptr<android::base::ScopedEventCallback<EventLoop, LooperStatusEvent>> mSubscription;
};

}  // namespace goldfish::async
