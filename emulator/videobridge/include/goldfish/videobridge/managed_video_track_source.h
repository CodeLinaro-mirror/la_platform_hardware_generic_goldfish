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
#include "api/notifier.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "media/base/video_broadcaster.h"
#pragma clang diagnostic pop

#include <optional>

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::videobridge {

/**
 * Base WebRTC video track source with automatic on-demand capture management.
 *
 * Automatically invokes OnStart() when the first sink is registered and OnStop()
 * when the last sink is unregistered.
 *
 * Thread Safety:
 * - Sink additions/removals and frame dispatches are thread-safe.
 */
class ManagedVideoTrackSource : public webrtc::Notifier<webrtc::VideoTrackSourceInterface> {
  public:
    ManagedVideoTrackSource() = default;
    ~ManagedVideoTrackSource() override = default;

    void AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
                         const webrtc::VideoSinkWants& wants) override {
        broadcaster_.AddOrUpdateSink(sink, wants);
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

    void RemoveSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override {
        broadcaster_.RemoveSink(sink);
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

    SourceState state() const override { return SourceState::kLive; }
    bool remote() const override { return false; }
    bool is_screencast() const override { return true; }
    std::optional<bool> needs_denoising() const override { return false; }

    bool SupportsEncodedOutput() const override { return false; }
    void GenerateKeyFrame() override {}
    void AddEncodedSink(webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>*) override {}
    void RemoveEncodedSink(webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>*) override {}
    bool GetStats(Stats*) override { return false; }

    /**
     * Explicitly starts capture if not already running.
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
     * Explicitly stops capture.
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

    bool has_sinks() const {
        absl::MutexLock lock(&sink_mutex_);
        return !sinks_.empty();
    }

  protected:
    void OnFrame(const webrtc::VideoFrame& frame) { broadcaster_.OnFrame(frame); }

    /**
     * Invoked when video capture should start.
     */
    virtual void OnStart() = 0;

    /**
     * Invoked when video capture should stop.
     */
    virtual void OnStop() = 0;

  private:
    webrtc::VideoBroadcaster broadcaster_;
    mutable absl::Mutex transition_mutex_;
    mutable absl::Mutex sink_mutex_ ABSL_ACQUIRED_AFTER(transition_mutex_);
    absl::flat_hash_set<webrtc::VideoSinkInterface<webrtc::VideoFrame>*> sinks_
            ABSL_GUARDED_BY(sink_mutex_);
    bool explicitly_running_ ABSL_GUARDED_BY(transition_mutex_) = false;
};

}  // namespace goldfish::videobridge
