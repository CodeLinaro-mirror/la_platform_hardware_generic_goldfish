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
#include "goldfish/async/libuv_event_loop.h"

#include <iostream>
#include <utility>

#include "absl/log/log.h"

#include "goldfish/async/uv_to_absl.h"

namespace goldfish::async {

LibuvEventLoop::LibuvEventLoop() {
    mLoop = new uv_loop_t();
    if (uv_loop_init(mLoop) != 0) {
        LOG(ERROR) << "Failed to initialize uv_loop.";
        delete mLoop;
        mLoop = nullptr;
        return;
    }
    mLoop->data = this;

    // Create and start the keep-alive handle, this will make
    // sure the loop does not immediately exit if there is no work
    // to be done.
    mKeepAliveHandle = new uv_idle_t();
    uv_idle_init(mLoop, mKeepAliveHandle);
    uv_idle_start(mKeepAliveHandle, [](uv_idle_t* handle) { /* No-op */ });

    // Initialize the async handle for thread-safe signaling.
    mAsyncHandle.data = this;
    uv_async_init(mLoop, &mAsyncHandle, [](uv_async_t* handle) {
        static_cast<LibuvEventLoop*>(handle->data)->processTasks();
    });
}

LibuvEventLoop::~LibuvEventLoop() {
    if (mLoop) {
        // Stop and close all handles.
        if (mKeepAliveHandle) {
            uv_close((uv_handle_t*)mKeepAliveHandle,
                     [](uv_handle_t* handle) { delete reinterpret_cast<uv_idle_t*>(handle); });
        }
        uv_close((uv_handle_t*)&mAsyncHandle, [](uv_handle_t* handle) {
            // The handle is a member, not heap-allocated, so no delete needed.
        });

        // Final run to process pending close callbacks.
        uv_run(mLoop, UV_RUN_NOWAIT);

        int res = uv_loop_close(mLoop);
        if (res != 0) {
            LOG(WARNING) << "failed to close uv_loop: " << uv_strerror(res);
        }
        delete mLoop;
    }
}

void LibuvEventLoop::processTasks() {
    std::queue<Task> tasks;
    {
        absl::MutexLock lock(&mTaskMutex);
        tasks = std::move(mTaskQueue);
    }
    while (!tasks.empty()) {
        tasks.front()();
        tasks.pop();
    }
}

bool LibuvEventLoop::isOnLoopThread() {
    return std::this_thread::get_id() == mThreadId;
}

absl::Status LibuvEventLoop::post(Task task) {
    {
        absl::MutexLock lock(&mTaskMutex);
        mTaskQueue.push(std::move(task));
    }
    uv_async_send(&mAsyncHandle);
    return absl::OkStatus();
}

void LibuvEventLoop::run() {
    mThreadId = std::this_thread::get_id();
    if (mLoop) {
        uv_run(mLoop, UV_RUN_DEFAULT);
    }
}

void LibuvEventLoop::stop() {
    if (mLoop) {
        uv_stop(mLoop);
    }
}

void* LibuvEventLoop::getRawLoop() const {
    return mLoop;
}

}  // namespace goldfish::async