// Copyright 2019 The Android Open Source Project
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
#include "android/base/system.h"
#include "android/crashreport/hang_detector.h"

namespace android {
namespace crashreport {

using base::System;

/**
 * @brief A hang detector that checks for hangs after a specified interval.
 *
 * This detector wraps another `StatefulHangdetector` and only performs the
 * inner detector's check after a specified interval has passed.  Once the inner
 * detector signals a hang, this detector will continue to signal a hang on
 * subsequent checks.
 */
class TimedHangDetector : public StatefulHangdetector {
  public:
    /**
     * @brief Constructs a TimedHangDetector.
     *
     * @param intervalMs The time interval, in milliseconds, between checks.
     * @param check The inner `StatefulHangdetector` to use for hang detection.
     */
    TimedHangDetector(System::Duration intervalMs, StatefulHangdetector* check);

    /**
     * @brief Checks for a hang condition.
     *
     * This method only calls the inner detector's `check()` method after the
     * specified interval has passed.
     *
     * @return `true` if a hang is detected, `false` otherwise.
     */
    bool Check() override;

  private:
    std::unique_ptr<StatefulHangdetector> mInner;
    bool mFailedOnce = false;
    System::Duration mIntervalMs = 0;
    System::Duration mNextCheck = 0;
};

/**
 * @brief  Detects hangs by monitoring the guest heartbeat.
 *
 * This detector monitors the guest heartbeat.  If the heartbeat stops
 * incrementing, a hang is detected.
 *
 * The system is considered to be booting if the heartbeat is 0.  No
 * heartbeat increments are expected during boot.
 *
 * @note Heartbeat values are assumed to be non-decreasing.
 */
class HeartBeatDetector : public StatefulHangdetector {
  public:
    /**
     * @brief Constructs a HeartBeatDetector.
     *
     * @param getHeartbeat A callable object that returns the current heartbeat value.
     */
    HeartBeatDetector(std::function<int()> getHeartbeat);

    /**
     * @brief Checks for a hang condition.
     * @return `true` if a hang is detected, `false` otherwise.
     */
    bool Check() override;

  private:
    std::function<int()> mGetHeartbeat;
    int64_t mBeatCount = 0;
};

}  // namespace crashreport
}  // namespace android
