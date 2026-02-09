// Copyright (C) 2022 The Android Open Source Project
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

#include <algorithm>
#include <deque>
#include <streambuf>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace goldfish {

/**
 * @brief A thread-safe, blocking stream buffer for producer-consumer patterns.
 *
 * BlockingStreamBuf implements a synchronized buffer that allows one or more
 * threads to write data while one or more threads read from it. If the buffer
 * is empty, readers will block until data becomes available or the stream
 * is closed via Close().
 *
 * @section perf Performance Characteristics
 * - **Writer Performance**: Appending data is an O(N) operation where N is the
 *   number of characters, primarily limited by std::deque insertion and mutex
 *   acquisition.
 * - **Reader Performance**: Blocking reads use absl::Condition for efficient
 *   waiting, avoiding busy-loops. Bulk reads (xsgetn) minimize lock contention
 *   by acquiring the mutex once per call.
 * - **Concurrency**: Scalable for multiple producers and a single consumer.
 *   Heavy contention between multiple readers and writers may impact throughput
 *   due to the shared lock.
 *
 * @tparam char_type The character type (e.g., char, wchar_t).
 */
template <typename char_type>
class BlockingStreamBuf : public std::basic_streambuf<char_type> {
  public:
    using traits_type = std::char_traits<char_type>;
    using int_type = typename traits_type::int_type;

    BlockingStreamBuf() : closed_(false) {
        // Force all access through virtual methods to ensure locking
        this->setp(nullptr, nullptr);
        this->setg(nullptr, nullptr, nullptr);
    }

    bool IsValid() const { return !closed_; }

    /**
     * @brief Closes the stream, signaling EOF to readers.
     *
     * This method sets the closed state and wakes up any threads waiting for data.
     * Readers will continue to receive data currently in the buffer. Once the
     * buffer is empty, subsequent reads will return EOF.
     */
    void Close() {
        absl::MutexLock lock(mutex_);
        closed_ = true;
    }

  protected:
    // --- WRITER SIDE ---
    int_type overflow(int_type c) override {
        if (c == traits_type::eof()) return c;
        absl::MutexLock lock(mutex_);
        buffer_.push_back(static_cast<char_type>(c));
        return c;
    }

    std::streamsize xsputn(const char_type* s, std::streamsize n) override {
        absl::MutexLock lock(mutex_);
        buffer_.insert(buffer_.end(), s, s + n);
        return n;
    }

    // --- READER SIDE ---
    int_type underflow() override {
        absl::MutexLock lock(mutex_);
        WaitForData();

        if (buffer_.empty()) return traits_type::eof();
        return traits_type::to_int_type(buffer_.front());
    }

    int_type uflow() override {
        absl::MutexLock lock(mutex_);
        WaitForData();

        if (buffer_.empty()) return traits_type::eof();

        int_type c = traits_type::to_int_type(buffer_.front());
        buffer_.pop_front();
        return c;
    }

    // Optimization: Bulk Read
    std::streamsize xsgetn(char_type* s, std::streamsize n) override {
        absl::MutexLock lock(mutex_);
        WaitForData();

        if (buffer_.empty()) return 0;  // EOF

        std::streamsize count = std::min(n, static_cast<std::streamsize>(buffer_.size()));
        std::copy(buffer_.begin(), buffer_.begin() + count, s);
        buffer_.erase(buffer_.begin(), buffer_.begin() + count);
        return count;
    }

  private:
    bool HasDataOrClosed() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
        return !buffer_.empty() || closed_;
    }

    void WaitForData() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
        mutex_.Await(absl::Condition(this, &BlockingStreamBuf::HasDataOrClosed));
    }

    absl::Mutex mutex_;
    bool closed_ ABSL_GUARDED_BY(mutex_){false};
    std::deque<char_type> buffer_ ABSL_GUARDED_BY(mutex_);
};
}  // namespace goldfish
