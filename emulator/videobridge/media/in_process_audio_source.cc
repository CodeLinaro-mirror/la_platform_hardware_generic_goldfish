// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/videobridge/in_process_audio_source.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::videobridge {

InProcessAudioSource::InProcessAudioSource(uint32_t sample_rate, uint32_t channels)
        : sample_rate_(sample_rate)
        , channels_(channels)
        , samples_per_10ms_channel_(sample_rate / 100)
        , samples_per_10ms_total_(static_cast<size_t>(sample_rate / 100) * channels)
        , partial_frame_(samples_per_10ms_total_) {
    DCHECK_GT(sample_rate, 0U) << "Audio sample rate must be greater than 0.";
    DCHECK_EQ(sample_rate % 100, 0U) << "Audio sample rate must be a multiple of 100 Hz.";
    DCHECK_GT(channels, 0U) << "Audio channels must be greater than 0.";
}

InProcessAudioSource::~InProcessAudioSource() {
    OnStop();
}

void InProcessAudioSource::OnStart() {
    running_ = true;
}

void InProcessAudioSource::OnStop() {
    running_ = false;
    const absl::MutexLock lock(&buffer_mutex_);

    // Make sure we don't get *crackles* on start/stop.
    partial_samples_ = 0;
}

void InProcessAudioSource::OnAudioData(const int16_t* pcm_data, size_t num_samples) {
    if (!running_ || !pcm_data || num_samples == 0) {
        return;
    }

    const absl::MutexLock lock(&buffer_mutex_);

    if (partial_samples_ > 0) {
        const size_t samples_needed = samples_per_10ms_total_ - partial_samples_;
        if (num_samples < samples_needed) {
            std::memcpy(partial_frame_.data() + partial_samples_, pcm_data,
                        num_samples * sizeof(int16_t));
            partial_samples_ += num_samples;
            return;
        }
        std::memcpy(partial_frame_.data() + partial_samples_, pcm_data,
                    samples_needed * sizeof(int16_t));
        Dispatch10msFrame(partial_frame_.data(), kBitsPerSample, sample_rate_, channels_,
                          samples_per_10ms_channel_);
        partial_samples_ = 0;
        pcm_data += samples_needed;
        num_samples -= samples_needed;
    }

    while (num_samples >= samples_per_10ms_total_) {
        Dispatch10msFrame(pcm_data, kBitsPerSample, sample_rate_, channels_,
                          samples_per_10ms_channel_);
        pcm_data += samples_per_10ms_total_;
        num_samples -= samples_per_10ms_total_;
    }

    if (num_samples > 0) {
        std::memcpy(partial_frame_.data(), pcm_data, num_samples * sizeof(int16_t));
        partial_samples_ = num_samples;
    }
}

}  // namespace goldfish::videobridge
