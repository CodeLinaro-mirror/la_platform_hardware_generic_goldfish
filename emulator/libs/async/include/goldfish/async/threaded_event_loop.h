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

#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"

namespace goldfish::async {

/**
 * @brief A decorator that runs an EventLoop implementation in a background thread.
 *
 * This class takes ownership of a concrete EventLoop implementation (e.g., a
 * LibuvEventLoop) and manages its execution in a dedicated worker thread. The
 * `run()` method of the underlying loop is called when the thread starts. The
 * destructor ensures the loop is cleanly shut down and the thread is joined.
 *
 * @note This class provides a simple way to turn any EventLoop implementation
 * into a fully-threaded, concurrently-running service.
 */
class ThreadedEventLoop : public EventLoop {
  public:
    using EventLoop::EventLoop;
    /**
     * @brief Virtual destructor.
     */
    ~ThreadedEventLoop() override = default;

    /**
     * @brief Gets the thread ID of the background event loop thread.
     *
     * This can be used to verify if the current code is executing on the event
     * loop's thread, similar to `isOnLoopThread()`.
     * @return The `std::thread::id` of the worker thread.
     */
    virtual std::thread::id GetId() const = 0;

    /**
     * @brief Gets the default timeout used during shutdown.
     *
     * The destructor will wait at most this amount of time for the underlying
     * loop to shut down cleanly.
     * @note Exceeding this timeout may indicate that resources like active
     * timers were leaked, preventing a graceful shutdown.
     * @return A `std::chrono::milliseconds` value representing the timeout.
     */
    static constexpr std::chrono::milliseconds GetTimeout() { return std::chrono::seconds(5); }

    /**
     * @brief Gets the current state of the event loop.
     * @return The current state.
     */
    LooperStatusEvent::State GetState() const override = 0;

    /**
     * @brief Creates and starts a new ThreadedEventLoop.
     *
     * This factory function constructs the event loop, starts its background
     * thread, and waits for it to initialize before returning.
     *
     * @param[in] toRun A `std::unique_ptr` to an `EventLoop` implementation
     * (e.g., `LibuvEventLoop`) that this class will own and manage.
     * @return A `std::unique_ptr` to the new `ThreadedEventLoop` on success, or
     * `nullptr` if the background thread fails to start in a timely manner.
     */
    static std::unique_ptr<ThreadedEventLoop> Create(std::unique_ptr<LibuvEventLoop> to_run);

  protected:
    virtual absl::Status Start() = 0;
};

}  // namespace goldfish::async
