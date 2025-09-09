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

#include "android/goldfish/display/FpsCalculator.h"

namespace android::goldfish {
FpsCalculator::FpsCalculator(int windowSize) : mWindowSize(windowSize) {
    mTimestamps.resize(windowSize);
}

void FpsCalculator::addFrame(absl::Time timestamp) {
    mTimestamps[mNextFrameIndex] = timestamp;
    mNextFrameIndex = (mNextFrameIndex + 1) % mWindowSize;
    if (mCurrentFrameCount < mWindowSize) {
        mCurrentFrameCount++;
    }
}

double FpsCalculator::getFps() const {
    if (mCurrentFrameCount < 2) {
        return 0.0;
    }

    // The FPS is calculated over a sliding window of timestamps.
    // The calculation is:
    //
    // FPS = (frame_count - 1) / (last_timestamp - first_timestamp)
    //
    // We use `mCurrentFrameCount - 1` because N frames yield N-1 time
    // intervals. The duration is converted to nanoseconds for the final
    // calculation.
    const int firstIndex = (mNextFrameIndex - mCurrentFrameCount + mWindowSize) % mWindowSize;
    const int lastIndex = (mNextFrameIndex - 1 + mWindowSize) % mWindowSize;

    const absl::Time firstTimestamp = mTimestamps[firstIndex];
    const absl::Time lastTimestamp = mTimestamps[lastIndex];
    const absl::Duration duration = lastTimestamp - firstTimestamp;

    if (duration == absl::ZeroDuration()) {
        return 0.0;
    }

    return (mCurrentFrameCount - 1) * 1e9 / absl::ToDoubleNanoseconds(duration);
}

}  // namespace android::goldfish
