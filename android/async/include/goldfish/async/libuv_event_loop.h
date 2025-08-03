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
#include <queue>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "uv.h"

namespace goldfish::async {

/**
 * @brief A concrete implementation of the EventLoop interface using libuv.
 *
 * This class manages the lifecycle of a uv_loop_t
 * and includes a keep-alive handle to ensure the
 * loop runs even when no other I/O handles are active.
 */
class LibuvEventLoop : public EventLoop {
  public:
    LibuvEventLoop();
    ~LibuvEventLoop() override;

    // --- Prevent Copying ---
    LibuvEventLoop(const LibuvEventLoop&) = delete;
    LibuvEventLoop& operator=(const LibuvEventLoop&) = delete;

    // --- Enable Moving ---
    LibuvEventLoop(LibuvEventLoop&& other) noexcept;
    LibuvEventLoop& operator=(LibuvEventLoop&& other) noexcept = delete;

    void run() override;
    void stop() override;
    bool isOnLoopThread() override;
    absl::Status post(Task task) override;

    void* getRawLoop() const override;
    void processTasks();

  private:
    uv_loop_t* mLoop = nullptr;
    uv_idle_t* mKeepAliveHandle = nullptr;
    uv_async_t mAsyncHandle;

    std::thread::id mThreadId;

    absl::Mutex mTaskMutex;
    std::queue<Task> mTaskQueue ABSL_GUARDED_BY(mTaskMutex);
};
}  // namespace goldfish::async