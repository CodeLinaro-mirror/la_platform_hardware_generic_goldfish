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
#include "api/scoped_refptr.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_rotation.h"
#include "common_video/include/video_frame_buffer_pool.h"
#pragma clang diagnostic pop

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/display/display.h"
#include "goldfish/display/latest_frame_pump.h"
#include "goldfish/videobridge/managed_video_track_source.h"

namespace goldfish::display {
class IMultiDisplay;
}

namespace goldfish::videobridge {

/**
 * Converts Goldfish counter-clockwise (CCW) ImageRotation to WebRTC clockwise (CW) VideoRotation.
 */
::webrtc::VideoRotation ToWebRtcRotation(goldfish::display::ImageRotation rotation);

/**
 * WebRTC video track source capturing guest display frames directly from IMultiDisplay in-process.
 *
 * Concept & Data Flow:
 * - Ingests a reference to IMultiDisplay and a target logical display ID.
 * - Reactive Activation: Lazily queries IMultiDisplay for the active IDisplay when the first
 *   WebRTC sink attaches (OnStart) and unsubscribes when all participants disconnect (OnStop).
 * - Subscribes to FrameEvents (including display rotation updates) from the active display via
 *   LatestFramePump, reading NV12 pixels from immutable SurfaceFrames on a dedicated worker loop
 *   and dispatching frames to attached WebRTC sinks.
 */
class InProcessVideoSource : public ManagedVideoTrackSource {
  public:
    /**
     * Constructs an in-process WebRTC video track source bound to a MultiDisplay coordinator.
     *
     * @param multidisplay Reference to the multidisplay coordinator.
     * @param display_id Logical display index to capture (default: 0).
     */
    explicit InProcessVideoSource(::goldfish::display::IMultiDisplay& multidisplay,
                                  uint32_t display_id = 0);

    ~InProcessVideoSource() override;

  protected:
    void OnStart() override;
    void OnStop() override;

  private:
    void ProcessFrame(::goldfish::display::FramePtr frame);

    ::goldfish::display::IMultiDisplay& multidisplay_;
    const uint32_t display_id_;
    ::webrtc::VideoFrameBufferPool buffer_pool_{/*zero_initialize=*/false,
                                                /*max_number_of_buffers=*/8};

    absl::Mutex frame_mutex_;
    std::shared_ptr<::goldfish::display::IDisplay> display_ ABSL_GUARDED_BY(frame_mutex_);
    std::unique_ptr<::goldfish::async::EventLoop> worker_ ABSL_GUARDED_BY(frame_mutex_);
    std::shared_ptr<::goldfish::display::LatestFramePump> pump_ ABSL_GUARDED_BY(frame_mutex_);
    ::goldfish::display::IDisplay::ScopedFrameListener listener_ ABSL_GUARDED_BY(frame_mutex_);
    uint64_t last_seq_ ABSL_GUARDED_BY(frame_mutex_) = 0;
    uint64_t total_dropped_ ABSL_GUARDED_BY(frame_mutex_) = 0;
};

}  // namespace goldfish::videobridge
