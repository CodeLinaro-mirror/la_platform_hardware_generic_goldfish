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

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "display_service.grpc.pb.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/display/display.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"
#include "goldfish/system/steady_clock.h"

namespace goldfish::grpc::v2 {

// DisplayManager.VIRTUAL_DISPLAY_FLAG_* bits. The guest passes display flags unchanged to
// VirtualDisplayConfig.Builder.setFlags().
inline constexpr uint32_t kAndroidFlagPublic = 1U << 0;
inline constexpr uint32_t kAndroidFlagPresentation = 1U << 1;
inline constexpr uint32_t kAndroidFlagSecure = 1U << 2;
inline constexpr uint32_t kAndroidFlagOwnContentOnly = 1U << 3;
inline constexpr uint32_t kAndroidFlagAutoMirror = 1U << 4;

::android::emulation::v2::Rotation ToProtoRotation(display::ImageRotation rotation);
absl::StatusOr<display::ImageRotation> FromProtoRotation(
        ::android::emulation::v2::Rotation rotation);
absl::StatusOr<::android::emulation::v2::ImageFormat> ResolveImageFormat(
        ::android::emulation::v2::ImageFormat requested);

/**
 * @brief Handles on-demand (`GetScreenshot`) and streaming (`StreamScreenshots`)
 *        frame capture, format encoding (PNG, JPEG, RAW_RGBA8888), and FPS throttling
 *        for `DisplayServiceImpl`.
 */
class ScreenshotCapturer {
  public:
    explicit ScreenshotCapturer(
            int32_t default_density_dpi = 420,
            const system::SteadyClock& clock = system::SteadyClock::GetRealClock());
    ~ScreenshotCapturer();

    ScreenshotCapturer(const ScreenshotCapturer&) = delete;
    ScreenshotCapturer& operator=(const ScreenshotCapturer&) = delete;

    ::grpc::Status CaptureScreenshot(const display::IDisplay& display,
                                     const ::android::emulation::v2::GetScreenshotRequest& request,
                                     ::android::emulation::v2::Screenshot* response) const;

    ::grpc::ServerWriteReactor<::android::emulation::v2::Screenshot>* StreamScreenshots(
            std::shared_ptr<display::IDisplay> display,
            const ::android::emulation::v2::StreamScreenshotsRequest& request) const;

    absl::Status EncodeFrame(const display::IDisplay& display, const display::SurfaceFrame& frame,
                             ::android::emulation::v2::ImageFormat format,
                             ::android::emulation::v2::Screenshot* out) const;

  private:
    struct SharedState;
    struct StreamState;
    std::shared_ptr<SharedState> shared_;
};

}  // namespace goldfish::grpc::v2
