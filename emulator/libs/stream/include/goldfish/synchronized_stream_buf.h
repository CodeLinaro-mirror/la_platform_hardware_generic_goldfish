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
#include <streambuf>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace goldfish {

/**
 * @brief A decorator that adds thread-safety to an existing std::streambuf.
 *
 * SynchronizedStreamBuf wraps a raw pointer to an underlying stream buffer and
 * protects all access to it using an internal mutex. This is useful when multiple
 * threads need to share a single output stream (like std::cout) without character
 * interleaving or when the underlying buffer is not inherently thread-safe.
 *
 * @section perf Performance Characteristics
 * - **Overhead**: Each operation (read, write, sync) incurs the cost of an
 *   absl::Mutex lock/unlock cycle.
 * - **Latency**: Directly proportional to the speed of the underlying buffer
 *   plus the synchronization overhead.
 * - **Throughput**: High for large bulk transfers (xsputn/xsgetn), as they
 *   only lock once per call. Character-at-a-time access (sputc/sgetc) will be
 *   significantly slower due to repeated lock acquisitions.
 *
 * @tparam char_type The character type (e.g., char, wchar_t).
 */
template <typename char_type>
class SynchronizedStreamBuf : public std::basic_streambuf<char_type> {
  public:
    using traits_type = std::char_traits<char_type>;
    using int_type = typename traits_type::int_type;

    // We do not own the inner buffer; we just protect it.
    explicit SynchronizedStreamBuf(std::basic_streambuf<char_type>* inner) : inner_(inner) {
        // Force all access through virtual methods to ensure locking
        this->setp(nullptr, nullptr);
        this->setg(nullptr, nullptr, nullptr);
    }

    bool IsValid() const {
        absl::MutexLock lock(&mutex_);
        return inner_ != nullptr;
    }

  protected:
    // --- WRITER SIDE ---
    int_type overflow(int_type c) override {
        absl::MutexLock lock(&mutex_);
        return inner_->sputc(c);
    }

    std::streamsize xsputn(const char_type* s, std::streamsize n) override {
        absl::MutexLock lock(&mutex_);
        return inner_->sputn(s, n);
    }

    int sync() override {
        absl::MutexLock lock(&mutex_);
        return inner_->pubsync();
    }

    // --- READER SIDE ---
    int_type underflow() override {
        absl::MutexLock lock(&mutex_);
        return inner_->sgetc();
    }

    int_type uflow() override {
        absl::MutexLock lock(&mutex_);
        return inner_->sbumpc();
    }

    std::streamsize xsgetn(char_type* s, std::streamsize n) override {
        absl::MutexLock lock(&mutex_);
        return inner_->sgetn(s, n);
    }

  private:
    mutable absl::Mutex mutex_;
    std::basic_streambuf<char_type>* inner_ ABSL_GUARDED_BY(mutex_);
};
}  // namespace goldfish