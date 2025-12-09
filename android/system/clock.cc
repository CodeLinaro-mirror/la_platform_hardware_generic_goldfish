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
#include "android/base/clock.h"

#include <mutex>

#include "absl/log/log.h"

#include "android/base/abseil_clock.h"

namespace android::base {

namespace {
// A global mutex is used to protect write access (initialization and
// replacement) of the clock instance.
std::mutex gLock;
}  // namespace

// The global clock instance is stored as an atomic pointer. This allows for
// lock-free reads in the common case (IClock::get), which is critical for
// performance.
std::atomic<IClock*> IClock::sInstance;

// A unique_ptr is used to manage the lifetime of the clock instance that this
// singleton "owns" (i.e., the one created by this class, or the one most
// recently passed to `set`).
std::unique_ptr<IClock> IClock::sOwnedInstance;

void IClock::set(std::unique_ptr<IClock> clock) {
    // A lock is required to safely replace the clock instance.
    std::lock_guard<std::mutex> lock(gLock);
    sOwnedInstance = std::move(clock);
    // Use memory_order_release to ensure that the write to sOwnedInstance is
    // visible to any other thread that subsequently acquires this pointer.
    sInstance.store(sOwnedInstance.get(), std::memory_order_release);
}

IClock& IClock::get() {
    // In the common case, the instance is already set. We can load the pointer
    // atomically without incurring the cost of a mutex lock.
    // memory_order_acquire ensures that we see the fully constructed object
    // that was stored with memory_order_release.
    IClock* instance = sInstance.load(std::memory_order_acquire);
    if (instance) {
        return *instance;
    }

    // If the instance is not set, we must acquire a lock to ensure that only
    // one thread creates the fallback instance.
    std::lock_guard<std::mutex> lock(gLock);
    // Now that we have the lock, we must check again to see if another thread
    // has set the instance while we were waiting. This is the "double-check"
    // in the double-checked locking pattern.
    instance = sInstance.load(std::memory_order_acquire);
    if (instance) {
        return *instance;
    }

    // If the instance is still null, this thread is responsible for creating it.
    LOG(WARNING) << "IClock not explicitly set, falling back to AbseilClock. "
                    "Virtual clock types will reflect host time.";
    sOwnedInstance = std::make_unique<AbseilClock>();
    instance = sOwnedInstance.get();
    // Release the newly created instance to other threads.
    sInstance.store(instance, std::memory_order_release);
    return *instance;
}
}  // namespace android::base
