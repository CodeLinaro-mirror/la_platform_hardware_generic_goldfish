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

#include <cassert>
#include <mutex>
#include <vector>

#include "goldfish/async/event_loop.h"
#include "goldfish/eventing/event_source.h"
#include "goldfish/eventing/event_sources.h"

namespace goldfish::async {

/**
 * @brief A dispatcher policy that posts event notifications to a goldfish::async::EventLoop.
 *
 * This policy ensures that event listeners are always notified on the thread
 * of the specified event loop. It is stateful and must be constructed with a
 * pointer to a valid EventLoop instance.
 *
 * The dispatching is non-blocking: it creates a copy of the listener list
 * and posts a task to the loop, preventing deadlocks.
 */
class EventLoopDispatcher {
  public:
    explicit EventLoopDispatcher(goldfish::async::EventLoop* loop) : loop_(loop) {
        assert(loop_ != nullptr);
    }

    template <class T, class StoragePolicy>
    void Dispatch(const T& event, typename StoragePolicy::Container& listeners, std::mutex& lock) {
        using Ptr = typename StoragePolicy::Ptr;
        std::vector<Ptr> listeners_copy;
        {
            const std::lock_guard<std::mutex> guard(lock);
            listeners_copy = StoragePolicy::Copy(listeners);
        }

        if (listeners_copy.empty()) {
            return;
        }

        auto dispatch_work = [event, listeners_copy = std::move(listeners_copy)]() {
            for (const auto& listener_ptr : listeners_copy) {
                android::base::eventing::EventDispatcher::Dispatch(listener_ptr, event);
            }
        };

        // If we are already on the loop thread, execute directly.
        if (loop_->IsOnLoopThread()) {
            dispatch_work();
        } else {
            loop_->Post(std::move(dispatch_work)).IgnoreError();
        }
    }

  private:
    goldfish::async::EventLoop* loop_;
};

/**
 * @brief A thread-safe, memory-safe event source that dispatches all events
 * onto a specific EventLoop.
 *
 * @details This is the recommended, easy-to-use alias for creating an event
 * source that is bound to an event loop. It combines the memory safety of
 * `SafeEventSource` (using `std::weak_ptr`) with the thread-safe dispatching
 * of the `EventLoopDispatcher`.
 *
 * @tparam T The event type.
 */
template <typename T>
using LoopBoundSafeSource = android::base::eventing::EventSource<
        T,
        android::base::eventing::HybridStoragePolicy<
                T, 16, std::weak_ptr<android::base::eventing::EventListener<T>>>,
        EventLoopDispatcher>;

/**
 * @brief A full-featured, loop-bound event source with a modern callback API.
 *
 * @details This alias layers the convenient `WithCallbacks` API on top of the
 * `LoopBoundSafeSource`, providing an easy-to-use interface with RAII-style
 * callback management, where all callbacks are guaranteed to execute on the
 * specified event loop.
 *
 * @tparam T The event type.
 */
template <typename T>
using LoopBoundCallbackSource = android::base::eventing::WithCallbacks<LoopBoundSafeSource<T>>;

}  // namespace goldfish::async
