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

#include "absl/log/check.h"
#include "absl/log/log.h"

namespace goldfish::videobridge {

InProcessAudioSource::InProcessAudioSource(uint32_t sample_rate, uint32_t channels)
        : sample_rate_(sample_rate)
        , channels_(channels)
        , samples_per_10ms_channel_(sample_rate / 100)
        , samples_per_10ms_total_((sample_rate / 100) * channels)
        , temp_frame_buffer_(samples_per_10ms_total_) {
    DCHECK_GT(sample_rate, 0u) << "Audio sample rate must be greater than 0.";
    DCHECK_EQ(sample_rate % 100, 0u) << "Audio sample rate must be a multiple of 100 Hz.";
    DCHECK_GT(channels, 0u) << "Audio channels must be greater than 0.";
}

InProcessAudioSource::~InProcessAudioSource() {
    absl::MutexLock lock(&sink_mutex_);
    DCHECK(sinks_.empty())
            << "All sinks must be unregistered before the audio source is destroyed.";
}

void InProcessAudioSource::AddSink(::webrtc::AudioTrackSinkInterface* sink) {
    if (!sink) return;
    absl::MutexLock lock(&sink_mutex_);
    sinks_.insert(sink);
}

void InProcessAudioSource::RemoveSink(::webrtc::AudioTrackSinkInterface* sink) {
    if (!sink) return;
    absl::MutexLock lock(&sink_mutex_);
    sinks_.erase(sink);
}

const ::webrtc::AudioOptions InProcessAudioSource::options() const {
    // Disable voice-centric DSP (AEC, AGC, NS, HPF) because this is direct system/guest audio;
    // processing would distort music, game effects, stereo imaging, dynamic range, and bass
    // frequencies.
    ::webrtc::AudioOptions options;
    options.echo_cancellation = false;
    options.auto_gain_control = false;
    options.noise_suppression = false;
    options.highpass_filter = false;
    return options;
}

void InProcessAudioSource::Start() {
    running_ = true;
}

void InProcessAudioSource::Stop() {
    running_ = false;
    absl::MutexLock lock(&buffer_mutex_);

    // Make sure we don't get *crackles* on start/stop.
    audio_buffer_.Clear(/*also_free_memory=*/true);
}

void InProcessAudioSource::OnAudioData(const int16_t* pcm_data, size_t num_samples) {
    if (!running_ || !pcm_data || num_samples == 0) {
        return;
    }

    absl::MutexLock lock(&buffer_mutex_);
    (void)audio_buffer_.Append(pcm_data, num_samples * sizeof(int16_t));

    const size_t bytes_per_10ms = samples_per_10ms_total_ * sizeof(int16_t);
    while (audio_buffer_.Size() >= bytes_per_10ms) {
        auto peek = audio_buffer_.Peek();
        if (peek.second >= bytes_per_10ms) {
            Dispatch10msFrame(reinterpret_cast<const int16_t*>(peek.first),
                              samples_per_10ms_channel_);
            (void)audio_buffer_.Consume(bytes_per_10ms);
        } else {
            // Handle ring buffer wrap-around: frame spans across the end of the buffer.
            uint8_t* dst = reinterpret_cast<uint8_t*>(temp_frame_buffer_.data());
            std::memcpy(dst, peek.first, peek.second);
            (void)audio_buffer_.Consume(peek.second);
            auto peek2 = audio_buffer_.Peek();
            std::memcpy(dst + peek.second, peek2.first, bytes_per_10ms - peek.second);
            Dispatch10msFrame(temp_frame_buffer_.data(), samples_per_10ms_channel_);
            (void)audio_buffer_.Consume(bytes_per_10ms - peek.second);
        }
    }
}

void InProcessAudioSource::Dispatch10msFrame(const int16_t* frame_data,
                                             size_t samples_per_channel) {
    // WebRTC unregisters sinks on worker/signaling threads, never within OnData() callbacks.
    // Holding sink_mutex_ during iteration ensures RemoveSink() blocks until active dispatches
    // complete, guaranteeing safe immediate sink destruction upon RemoveSink() return.
    absl::MutexLock lock(&sink_mutex_);
    for (auto* sink : sinks_) {
        sink->OnData(frame_data, /*bits_per_sample=*/16, sample_rate_, channels_,
                     samples_per_channel);
    }
}

}  // namespace goldfish::videobridge
