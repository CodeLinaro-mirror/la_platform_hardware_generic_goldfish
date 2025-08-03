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
#include <memory>
#include <thread>

#include "absl/status/status.h"

#include "goldfish/async/event_loop.h"

namespace goldfish::async {

/**
 * @brief A decorator that runs an EventLoop implementation in a background thread.
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
    explicit ThreadedEventLoop(std::unique_ptr<EventLoop> loop);
    ~ThreadedEventLoop() override;

    // --- Prevent Copying ---
    ThreadedEventLoop(const ThreadedEventLoop&) = delete;
    ThreadedEventLoop& operator=(const ThreadedEventLoop&) = delete;

    // --- Enable Moving ---
    ThreadedEventLoop(ThreadedEventLoop&& other) noexcept;
    ThreadedEventLoop& operator=(ThreadedEventLoop&& other) noexcept;

    /**
     * @brief Starts the background thread and begins executing the underlying
     * event loop's run() method within it. This method returns immediately.
     */
    void run() override;

    /**
     * @brief Stops the underlying event loop and waits for the background
     * thread to complete its execution. This is a blocking call.
     */
    void stop() override;

    /**
     * @brief Checks if the caller is on the background event loop thread.
     * @return Delegates the call to the underlying EventLoop.
     */
    bool isOnLoopThread() override;

    /**
     * @brief Posts a task to the underlying event loop to be executed on its
     * background thread.
     * @param task The task to execute.
     * @return The status of the post operation.
     */
    absl::Status post(Task task) override;

    void* getRawLoop() const override { return mLoop->getRawLoop(); }

  private:
    std::thread mRunner;
    std::unique_ptr<EventLoop> mLoop;
};

}  // namespace goldfish::async