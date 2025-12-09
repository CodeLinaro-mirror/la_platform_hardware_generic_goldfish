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

#include "android/base/clock.h"

namespace android::base {

/**
 * @brief A mock implementation of the IClock interface for testing.
 *
 * This class allows for manual control over the current time, making it
 * easy to test time-dependent logic.
 */
class TestClock : public IClock {
  public:
    absl::Time now(ClockType type) const override {
        // In a test environment, we don't need to distinguish between
        // different clock types. We just return the time that has been set.
        return mCurrentTime;
    }

    /**
     * @brief Sets the current time of the mock clock.
     * @param new_time The new time to set.
     */
    void set_time(absl::Time new_time) { mCurrentTime = new_time; }

    /**
     * @brief Advances the clock by a specified duration.
     * @param duration The duration to advance the clock by.
     */
    void advance(absl::Duration duration) { mCurrentTime += duration; }

  private:
    absl::Time mCurrentTime;
};

}  // namespace android::base
