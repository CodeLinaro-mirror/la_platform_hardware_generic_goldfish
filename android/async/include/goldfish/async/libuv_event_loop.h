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

#include "goldfish/async/event_loop.h"

namespace goldfish::async {

/**
 * @brief A concrete implementation of the EventLoop interface using libuv.
 * @class LibuvEventLoop
 *
 * This class provides a thread-safe event loop that manages asynchronous tasks,
 * delayed posts, and repeating timers. It encapsulates the libuv C library,
 * exposing its functionality through the abstract EventLoop interface.
 */
class LibuvEventLoop : public EventLoop {
  public:
    ~LibuvEventLoop() override = default;

    /**
     * @brief Runs the event loop, blocking until shutdown() is called.
     */
    virtual absl::Status Run() = 0;
    // These are made public for direct access by ThreadedEventLoop.
    absl::Status PostDelayed(Task task, std::chrono::milliseconds delay) override = 0;
    absl::Status PostImmediately(Task task) override = 0;

    static std::unique_ptr<LibuvEventLoop> Create();
};

}  // namespace goldfish::async