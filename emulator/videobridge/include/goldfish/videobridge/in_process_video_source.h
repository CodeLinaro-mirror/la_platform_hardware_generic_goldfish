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
#pragma clang diagnostic pop

#include <cstdint>
#include <memory>

#include "goldfish/display/display.h"
#include "goldfish/eventing/with_callbacks.h"
#include "goldfish/videobridge/managed_video_track_source.h"

namespace goldfish::display {
class IMultiDisplay;
}

namespace goldfish::videobridge {

/**
 * WebRTC video track source capturing guest display frames directly from IMultiDisplay in-process.
 *
 * Concept & Data Flow:
 * - Ingests a reference to IMultiDisplay and a target logical display ID.
 * - Reactive Activation: Lazily queries IMultiDisplay for the active IDisplay when the first
 *   WebRTC sink attaches (OnStart) and unsubscribes when all participants disconnect (OnStop).
 * - Subscribes to FrameInfo updates from the active display, converting raw BGRA/RGBA pixels
 *   to NV12 format and dispatching frames to attached WebRTC sinks.
 *
 * Thread Safety:
 * - InProcessVideoSource methods (OnStart, OnStop) are marshalled on WebRTC signaling threads.
 * - Frame arrival (OnFrameAvailable) occurs on QEMU display / render worker threads.
 * - Sink registration is synchronized via ManagedVideoTrackSource.
 *
 * Ownership & Lifetimes:
 * - Managed via webrtc::scoped_refptr (implements webrtc::VideoTrackSourceInterface).
 * - Holds a non-owning reference to IMultiDisplay, which must outlive this source.
 * - Holds a temporary shared_ptr handle to IDisplay only while actively streaming.
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
    void OnFrameAvailable(const ::goldfish::display::FrameInfo& frame_info);

    ::goldfish::display::IMultiDisplay& multidisplay_;
    const uint32_t display_id_;
    std::shared_ptr<::goldfish::display::IDisplay> display_;
    std::unique_ptr<android::base::eventing::ScopedEventCallback<
            ::goldfish::display::FrameInfoCallbackSource, ::goldfish::display::FrameInfo>>
            subscription_;
};

}  // namespace goldfish::videobridge
