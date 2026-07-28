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

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::videobridge::java {

class FrameBufferPool;

class NativeFrameBuffer : public std::enable_shared_from_this<NativeFrameBuffer> {
  public:
    NativeFrameBuffer(uint64_t id, size_t capacity, std::weak_ptr<FrameBufferPool> owner);
    ~NativeFrameBuffer() = default;

    uint64_t id() const { return id_; }
    uint8_t* data() { return buffer_.data(); }
    const uint8_t* data() const { return buffer_.data(); }
    size_t capacity() const { return buffer_.size(); }
    void EnsureCapacity(size_t required_capacity);
    void SetOwner(std::weak_ptr<FrameBufferPool> owner) { owner_ = std::move(owner); }
    void Release();

  private:
    uint64_t id_;
    std::vector<uint8_t> buffer_;
    std::weak_ptr<FrameBufferPool> owner_;
};

class FrameBufferPool : public std::enable_shared_from_this<FrameBufferPool> {
  public:
    explicit FrameBufferPool(size_t initial_pool_size = 4);
    ~FrameBufferPool() = default;

    std::shared_ptr<NativeFrameBuffer> Acquire(size_t required_capacity);
    void Release(uint64_t buffer_id);

  private:
    absl::Mutex mutex_;
    uint64_t next_id_ ABSL_GUARDED_BY(mutex_) = 1;
    std::vector<std::shared_ptr<NativeFrameBuffer>> available_buffers_ ABSL_GUARDED_BY(mutex_);
    std::unordered_map<uint64_t, std::shared_ptr<NativeFrameBuffer>> active_buffers_
            ABSL_GUARDED_BY(mutex_);
};

}  // namespace goldfish::videobridge::java
