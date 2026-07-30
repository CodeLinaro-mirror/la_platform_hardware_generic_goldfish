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
#include "api/video/i420_buffer.h"
#include "media/base/adapted_video_track_source.h"
#pragma clang diagnostic pop

#include <atomic>
#include <memory>
#include <vector>

#include "goldfish/display/display.h"
#include "goldfish/eventing/with_callbacks.h"

namespace goldfish::videobridge {

/**
 * @class InProcessVideoSource
 * @brief Custom AdaptedVideoTrackSource capturing frames directly from IDisplay in-memory.
 *
 * Receives direct C++ FrameInfo callbacks from QEMU's display subsystem and converts
 * raw surface memory into WebRTC video frames zero-copy.
 */
class InProcessVideoSource : public ::webrtc::AdaptedVideoTrackSource {
  public:
    explicit InProcessVideoSource(std::shared_ptr<::goldfish::display::IDisplay> display);
    ~InProcessVideoSource() override;

    // AdaptedVideoTrackSource overrides.
    bool is_screencast() const override { return true; }
    absl::optional<bool> needs_denoising() const override { return false; }
    ::webrtc::MediaSourceInterface::SourceState state() const override {
        return ::webrtc::MediaSourceInterface::SourceState::kLive;
    }
    bool remote() const override { return false; }

    /**
     * @brief Registers the display frame callback and starts frame capture.
     */
    void Start();

    /**
     * @brief Unregisters the display frame callback and stops frame capture.
     */
    void Stop();

  private:
    void OnFrameAvailable(const ::goldfish::display::FrameInfo& frame_info);

    std::shared_ptr<::goldfish::display::IDisplay> display_;
    std::unique_ptr<android::base::eventing::ScopedEventCallback<
            ::goldfish::display::FrameInfoCallbackSource, ::goldfish::display::FrameInfo>>
            subscription_;
    std::atomic<bool> running_{false};
    ::webrtc::scoped_refptr<::webrtc::I420Buffer> i420_buffer_;
};

}  // namespace goldfish::videobridge
