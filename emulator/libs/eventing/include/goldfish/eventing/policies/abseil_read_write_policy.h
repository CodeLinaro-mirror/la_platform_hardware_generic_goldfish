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

#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/eventing/event_source.h"
#include "goldfish/eventing/policies/pointer_handlers.h"

namespace android::base::eventing {

/**
 * @brief A self-synchronizing policy using an `absl::Mutex` as a read-write lock.
 * @details This policy manages its own concurrency. It uses a read-write lock
 * to allow multiple reader threads (`FireEvent`) to execute in parallel
 * without blocking each other. Writer threads (`addListener`, `removeListener`)
 * must acquire an exclusive lock.
 *
 * @par Performance
 * **Best for:** Highly concurrent, "read-heavy" or mixed read/write workloads.
 * It scales exceptionally well with multiple threads, showing minimal
 * performance degradation under contention.
 * @note Must be used with a lock-free host class like `ConcurrentEventSource`.
 */
template <class T, class PointerType = EventListener<T>*>
struct AbseilReadWritePolicy {
    using Ptr = PointerType;
    using Handlers = PointerHandlers<Ptr>;

    struct Storage {
        mutable absl::Mutex mutex;
        absl::flat_hash_set<Ptr, typename Handlers::Hash, typename Handlers::Equal> listeners
                ABSL_GUARDED_BY(mutex);
    };
    using Container = Storage;

    static void Add(Container& storage, const Ptr& listener) {
        const absl::MutexLock lock(&storage.mutex);
        storage.listeners.insert(listener);
    }
    static void Remove(Container& storage, const Ptr& listener) {
        const absl::MutexLock lock(&storage.mutex);
        storage.listeners.erase(listener);
    }
    static std::vector<Ptr> Copy(const Container& storage) {
        const absl::ReaderMutexLock lock(&storage.mutex);
        return {storage.listeners.begin(), storage.listeners.end()};
    }
    static size_t Size(const Container& storage) {
        const absl::ReaderMutexLock lock(&storage.mutex);
        return storage.listeners.size();
    }
    static void Clear(Container& storage) {
        const absl::MutexLock lock(&storage.mutex);
        storage.listeners.clear();
    }
};

template <class T, class PointerType = EventListener<T>*>
using AbseilSource = ConcurrentEventSource<T, AbseilReadWritePolicy<T, PointerType>>;

template <class T>
using WeakPtrAbseilSource =
        ConcurrentEventSource<T, AbseilReadWritePolicy<T, std::weak_ptr<EventListener<T>>>>;

}  // namespace android::base::eventing