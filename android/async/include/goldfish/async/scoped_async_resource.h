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

    explicit ScopedAsyncResource(std::shared_ptr<T> resource) : resource_(std::move(resource)) {
        if (resource_) {
            loop_ = resource_->GetLoop();
        }
    }

    ~ScopedAsyncResource() {
        if (resource_ && loop_) {
            if (loop_->IsOnLoopThread()) {
                // We are on the loop thread, so we can't block.
                resource_->Close();
            } else {
                // We are on a different thread. It's safe to block.
                loop_->PostAndWait([res = resource_]() { res->Close(); });
            }
        }
    }

    // --- Move semantics ---
    ScopedAsyncResource(ScopedAsyncResource&& other) noexcept
            : resource_(std::move(other.resource_)), loop_(other.loop_) {
        other.loop_ = nullptr;
    }

    ScopedAsyncResource& operator=(ScopedAsyncResource&& other) noexcept {
        if (this != &other) {
            if (resource_ && loop_) {
                loop_->PostAndWait([res = std::move(resource_)]() { res->Close(); });
            }
            resource_ = std::move(other.resource_);
            loop_ = other.loop_;
            other.loop_ = nullptr;
        }
        return *this;
    }

    // --- Deleted copy semantics ---
    ScopedAsyncResource(const ScopedAsyncResource&) = delete;
    ScopedAsyncResource& operator=(const ScopedAsyncResource&) = delete;

    // --- Accessors ---
    T* get() const { return resource_.get(); }  // NOLINT
    T* operator->() const { return resource_.get(); }
    explicit operator bool() const { return resource_ != nullptr; }

    // --- Manual Release ---
    std::shared_ptr<T> release() {  // NOLINT
        loop_ = nullptr;
        return std::move(resource_);
    }

  private:
    std::shared_ptr<T> resource_;
    EventLoop* loop_ = nullptr;
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