// Copyright 2017 The Android Open Source Project
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
#include "android/crashreport/crash_detectors.h"
namespace android {
namespace crashreport {

TimedHangDetector::TimedHangDetector(System::Duration intervalMs, StatefulHangdetector* check)
        : mInner(check), mIntervalMs(intervalMs) {
    mNextCheck = System::Get()->GetUnixTimeUs() + (mIntervalMs * 1000);
}

bool TimedHangDetector::Check() {
    if (System::Get()->GetUnixTimeUs() < mNextCheck) {
        return mFailedOnce;
    }
    mNextCheck = System::Get()->GetUnixTimeUs() + (mIntervalMs * 1000);
    mFailedOnce = mFailedOnce || mInner->Check();
    return mFailedOnce;
}

HeartBeatDetector::HeartBeatDetector(std::function<int()> getHeartbeat)
        : mGetHeartbeat(getHeartbeat) {}

bool HeartBeatDetector::Check() {
    int now = mGetHeartbeat();
    // Not every image will send a heartbeat, and hence
    // it will always return 0.
    if (mBeatCount == 0) {
        mBeatCount = now;
        // we have not received any yet;
        return false;
    }
    if (mBeatCount >= now) {
        // we detected a real problem
        return true;
    }
    mBeatCount = now;
    return false;
}

}  // namespace crashreport
}  // namespace android
