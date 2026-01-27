// Copyright (C) 2024 The Android Open Source Project
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
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace android {
namespace base {
/**
 * @brief Stores and manages the previous and last values of a property.
 *
 * This class provides a thread-safe way to track changes in a property
 * of type T. It maintains two values: the previous value (before the last
 * update) and the last value (after the last update).
 *
 * @tparam T The property type.
 *
 * @example
 * PropertyChangeEvent<std::pair<int, int>> position;
 * position.update({1, 2}); // Initial position
 * // ... later ...
 * position.update({3, 2});
 * auto lastPos = position.last(); // {3, 2}
 * auto prevPos = position.prev(); // {1, 2}
 * if (lastPos.first != prevPos.first) {
 *     // React to change in the first element of the pair.
 * }
 */
template <typename T>
class PropertyChangeEvent {
  public:
    PropertyChangeEvent() : prev_(), last_() {}
    explicit PropertyChangeEvent(T initial_value) : prev_(), last_(std::move(initial_value)) {}

    /**
     * @brief Updates the property with the latest value.
     *
     * This method sets the previous value to the current last value and then
     * updates the last value with the provided latest value. This operation
     * is thread-safe.
     *
     * @param latest The latest value of the property.
     */
    void Update(const T& latest) {
        const absl::MutexLock lock(&mutex_);
        prev_ = last_;
        last_ = latest;
    }

    /**
     * @brief Retrieves the previous value of the property.
     *
     * This method returns a copy of the previous value of the property.
     * This operation is thread-safe.
     *
     * @return A copy of the previous property value.
     */
    T Prev() const {
        const absl::MutexLock lock(&mutex_);
        return prev_;
    }

    /**
     * @brief Retrieves the last value of the property.
     *
     * This method returns a copy of the last value of the property.
     * This operation is thread-safe.
     *
     * @return A copy of the last property value.
     */
    T Last() const {
        const absl::MutexLock lock(&mutex_);
        return last_;
    }

  private:
    mutable absl::Mutex mutex_;  // protects prev_ and last_
    T prev_ ABSL_GUARDED_BY(mutex_);
    T last_ ABSL_GUARDED_BY(mutex_);
};
}  // namespace base
}  // namespace android