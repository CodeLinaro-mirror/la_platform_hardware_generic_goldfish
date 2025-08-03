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

#include "goldfish/async/event_loop.h"

namespace goldfish::async {

/**
 * @brief A generic, thread-safe RAII wrapper for an asynchronous resource.
 *
 * This template ensures that a resource's close() method is reliably called
 * when the object goes out of scope. It works for any type `T` that provides
 * `close()` and `getLoop()` methods.
 *
 * @tparam T The type of asynchronous resource to manage (e.g., AsyncSocket).
 */
template <typename T>
class ScopedAsyncResource {
  public:
    ScopedAsyncResource() = default;

    explicit ScopedAsyncResource(std::shared_ptr<T> resource) : mResource(std::move(resource)) {
        if (mResource) {
            mLoop = mResource->getLoop();
        }
    }

    ~ScopedAsyncResource() {
        if (mResource && mLoop) {
            if (mLoop->isOnLoopThread()) {
                // We are on the loop thread, so we can't block.
                mResource->close();
            } else {
                // We are on a different thread. It's safe to block.
                mLoop->postAndWait([res = mResource]() { res->close(); });
            }
        }
    }

    // --- Move semantics ---
    ScopedAsyncResource(ScopedAsyncResource&& other) noexcept
            : mResource(std::move(other.mResource)), mLoop(other.mLoop) {
        other.mLoop = nullptr;
    }

    ScopedAsyncResource& operator=(ScopedAsyncResource&& other) noexcept {
        if (this != &other) {
            if (mResource && mLoop) {
                mLoop->postAndWait([res = std::move(mResource)]() { res->close(); });
            }
            mResource = std::move(other.mResource);
            mLoop = other.mLoop;
            other.mLoop = nullptr;
        }
        return *this;
    }

    // --- Deleted copy semantics ---
    ScopedAsyncResource(const ScopedAsyncResource&) = delete;
    ScopedAsyncResource& operator=(const ScopedAsyncResource&) = delete;

    // --- Accessors ---
    T* get() const { return mResource.get(); }
    T* operator->() const { return mResource.get(); }
    explicit operator bool() const { return mResource != nullptr; }

    // --- Manual Release ---
    std::shared_ptr<T> release() {
        mLoop = nullptr;
        return std::move(mResource);
    }

  private:
    std::shared_ptr<T> mResource;
    EventLoop* mLoop = nullptr;
};

template <typename T>
bool operator==(const ScopedAsyncResource<T>& lhs, std::nullptr_t) noexcept {
    return !lhs;
}

template <typename T>
bool operator==(std::nullptr_t, const ScopedAsyncResource<T>& rhs) noexcept {
    return !rhs;
}

template <typename T>
bool operator!=(const ScopedAsyncResource<T>& lhs, std::nullptr_t) noexcept {
    return static_cast<bool>(lhs);
}

template <typename T>
bool operator!=(std::nullptr_t, const ScopedAsyncResource<T>& rhs) noexcept {
    return static_cast<bool>(rhs);
}
}  // namespace goldfish::async