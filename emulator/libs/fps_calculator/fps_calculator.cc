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

#include "goldfish/fps_calculator.h"

namespace goldfish {

FpsCalculator::FpsCalculator(int window_size) : window_size_(window_size) {
    timestamps_.resize(window_size);
}

void FpsCalculator::AddFrame(absl::Time timestamp) {
    timestamps_[next_frame_index_] = timestamp;
    next_frame_index_ = (next_frame_index_ + 1) % window_size_;
    if (current_frame_count_ < window_size_) {
        current_frame_count_++;
    }
}

double FpsCalculator::GetFps() const {
    if (current_frame_count_ < 2) {
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
    const int first_index =
            (next_frame_index_ - current_frame_count_ + window_size_) % window_size_;
    const int last_index = (next_frame_index_ - 1 + window_size_) % window_size_;

    const absl::Time first_timestamp = timestamps_[first_index];
    const absl::Time last_timestamp = timestamps_[last_index];
    const absl::Duration duration = last_timestamp - first_timestamp;

    if (duration == absl::ZeroDuration()) {
        return 0.0;
    }

    return (current_frame_count_ - 1) * 1e9 / absl::ToDoubleNanoseconds(duration);
}

}  // namespace goldfish
