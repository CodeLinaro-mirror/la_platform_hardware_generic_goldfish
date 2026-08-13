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
#pragma once

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/media_stream_interface.h"
#pragma clang diagnostic pop

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::videobridge {

/**
 * Base WebRTC audio track source with sink tracking and automatic capture management.
 *
 * Automatically invokes OnStart() when the first sink is registered and OnStop()
 * when the last sink is unregistered.
 *
 * Thread Safety:
 * - Sink additions/removals and frame dispatches are thread-safe.
 */
class ManagedAudioTrackSource : public ::webrtc::AudioSourceInterface {
  public:
    ManagedAudioTrackSource() = default;
    ~ManagedAudioTrackSource() override = default;

    void AddSink(::webrtc::AudioTrackSinkInterface* sink) override {
        if (!sink) return;
        absl::MutexLock transition_lock(&transition_mutex_);
        bool should_start = false;
        {
            absl::MutexLock lock(&sink_mutex_);
            sinks_.insert(sink);
            if (sinks_.size() == 1) {
                should_start = true;
            }
        }
        if (should_start) {
            OnStart();
        }
    }

    void RemoveSink(::webrtc::AudioTrackSinkInterface* sink) override {
        if (!sink) return;
        absl::MutexLock transition_lock(&transition_mutex_);
        bool should_stop = false;
        {
            absl::MutexLock lock(&sink_mutex_);
            sinks_.erase(sink);
            if (sinks_.empty()) {
                should_stop = true;
            }
        }
        if (should_stop) {
            OnStop();
        }
    }

    const ::webrtc::AudioOptions options() const override {
        ::webrtc::AudioOptions opts;
        opts.echo_cancellation = false;
        opts.auto_gain_control = false;
        opts.noise_suppression = false;
        opts.highpass_filter = false;
        return opts;
    }

    SourceState state() const override { return SourceState::kLive; }
    bool remote() const override { return false; }
    void RegisterObserver(::webrtc::ObserverInterface*) override {}
    void UnregisterObserver(::webrtc::ObserverInterface*) override {}

    /**
     * Explicitly starts audio capture if not already running.
     */
    void Start() {
        absl::MutexLock transition_lock(&transition_mutex_);
        bool should_start = false;
        {
            absl::MutexLock lock(&sink_mutex_);
            if (!explicitly_running_) {
                explicitly_running_ = true;
                should_start = true;
            }
        }
        if (should_start) {
            OnStart();
        }
    }

    /**
     * Explicitly stops audio capture.
     */
    void Stop() {
        absl::MutexLock transition_lock(&transition_mutex_);
        bool should_stop = false;
        {
            absl::MutexLock lock(&sink_mutex_);
            if (explicitly_running_ || !sinks_.empty()) {
                explicitly_running_ = false;
                should_stop = true;
            }
        }
        if (should_stop) {
            OnStop();
        }
    }

  protected:
    /**
     * Dispatches a 10ms PCM audio frame to all registered sinks.
     */
    void Dispatch10msFrame(const void* audio_data, int bits_per_sample, int sample_rate,
                           size_t number_of_channels, size_t number_of_frames) {
        absl::MutexLock lock(&sink_mutex_);
        for (auto* sink : sinks_) {
            sink->OnData(audio_data, bits_per_sample, sample_rate, number_of_channels,
                         number_of_frames);
        }
    }

    bool has_sinks() const {
        absl::MutexLock lock(&sink_mutex_);
        return !sinks_.empty();
    }

    /**
     * Invoked when audio capture should start.
     */
    virtual void OnStart() = 0;

    /**
     * Invoked when audio capture should stop.
     */
    virtual void OnStop() = 0;

  private:
    mutable absl::Mutex transition_mutex_;
    mutable absl::Mutex sink_mutex_ ABSL_ACQUIRED_AFTER(transition_mutex_);
    absl::flat_hash_set<::webrtc::AudioTrackSinkInterface*> sinks_ ABSL_GUARDED_BY(sink_mutex_);
    bool explicitly_running_ ABSL_GUARDED_BY(transition_mutex_) = false;
};

}  // namespace goldfish::videobridge
