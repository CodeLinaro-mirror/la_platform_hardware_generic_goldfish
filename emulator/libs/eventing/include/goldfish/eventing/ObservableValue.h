// Copyright 2025 The Android Open Source Project
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

#include <mutex>

#include "aemu/base/events/EventSources.h"

namespace goldfish::eventing {

struct ObservableValueTriggerAlways {
    template <class T>
    static constexpr bool updated(const T& oldVal, const T& newVal) {
        return true;
    }
};

struct ObservableValueTriggerOnUpdate {
    template <class T>
    static constexpr bool updated(const T& oldVal, const T& newVal) {
        return oldVal != newVal;
    }
};

template <class T, class TRIGGER>
struct ObservableValue : public android::base::eventing::CallbackEventSource<T> {
    using EventType = T;
    using CallbackId = android::base::eventing::CallbackEventSource<EventType>::CallbackId;

    ObservableValue(T val) : mValue(std::move(val)) {}

    ObservableValue() = default;
    ObservableValue(const ObservableValue&) = default;
    ObservableValue(ObservableValue&&) = default;
    ObservableValue& operator=(const ObservableValue&) = default;
    ObservableValue& operator=(ObservableValue&&) = default;

    void setValue(T newVal, const bool forceTrigger = false) {
        std::lock_guard<std::mutex> lock(mMtx);
        if (forceTrigger || TRIGGER::updated(mValue, newVal)) {
            mValue = std::move(newVal);
            this->fireEvent(mValue);
        }
    }

    T getValue() const {
        std::lock_guard<std::mutex> lock(mMtx);
        return mValue;
    }

  private:
    T mValue;
    mutable std::mutex mMtx;
};

}  // namespace goldfish::eventing
