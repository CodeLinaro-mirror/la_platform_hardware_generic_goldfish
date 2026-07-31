// Copyright (C) 2026 The Android Open Source Project
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

#include "frame_buffer_pool.h"

#include <algorithm>

namespace goldfish::videobridge::java {

NativeFrameBuffer::NativeFrameBuffer(uint64_t id, size_t capacity,
                                     std::weak_ptr<FrameBufferPool> owner)
        : id_(id), buffer_(capacity), owner_(std::move(owner)) {}

void NativeFrameBuffer::EnsureCapacity(size_t required_capacity) {
    if (buffer_.size() < required_capacity) {
        buffer_.resize(required_capacity);
    }
}

void NativeFrameBuffer::Release() {
    if (auto pool = owner_.lock()) {
        pool->Release(id_);
    }
}

FrameBufferPool::FrameBufferPool(size_t initial_pool_size) {
    for (size_t i = 0; i < initial_pool_size; ++i) {
        available_buffers_.push_back(std::make_shared<NativeFrameBuffer>(
                next_id_++, 0, std::weak_ptr<FrameBufferPool>()));
    }
}

std::shared_ptr<NativeFrameBuffer> FrameBufferPool::Acquire(size_t required_capacity) {
    absl::MutexLock lock(&mutex_);

    std::shared_ptr<NativeFrameBuffer> buffer;
    if (!available_buffers_.empty()) {
        buffer = available_buffers_.back();
        available_buffers_.pop_back();
    } else {
        buffer = std::make_shared<NativeFrameBuffer>(next_id_++, required_capacity,
                                                     weak_from_this());
    }

    buffer->EnsureCapacity(required_capacity);
    buffer->SetOwner(weak_from_this());
    active_buffers_[buffer->id()] = buffer;
    return buffer;
}

void FrameBufferPool::Release(uint64_t buffer_id) {
    absl::MutexLock lock(&mutex_);

    auto it = active_buffers_.find(buffer_id);
    if (it != active_buffers_.end()) {
        available_buffers_.push_back(std::move(it->second));
        active_buffers_.erase(it);
    }
}

}  // namespace goldfish::videobridge::java
