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

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "android/emulation/control/grpc_event_stream_support.h"
#include "display/display_limits.h"
#include "display/screenshot_capturer.h"
#include "display_service.grpc.pb.h"
#include "goldfish/avd/avd_universe.h"
#include "goldfish/display/abstract_multi_display.h"
#include "goldfish/display/display.h"
#include "goldfish/display/geometry.h"
#include "goldfish/eventing/event_sources.h"
#include "goldfish/sensors/foldable.h"
#include "goldfish/sensors/foldable_model.h"
#include "goldfish/sensors/physical_model.h"
#include "goldfish/system/steady_clock.h"

namespace goldfish::grpc::v2 {

struct DisplayServiceOptions {
    int32_t default_density_dpi = kDefaultDensityDpi;
    int32_t max_supported_display_count = kMaxSupportedDisplayCount;
    bool supports_virtual_display_creation = kSupportsVirtualDisplayCreation;
    bool read_only = false;
};

/**
 * @brief Implements the AEMU v2 DisplayService gRPC interface.
 *
 * Bridges `display::IMultiDisplay`, `display::IDisplay`, `sensors::PhysicalModel`,
 * and `ScreenshotCapturer` to provide display topology discovery, reactive state
 * streaming, virtual display lifecycle management, and screen capture.
 *
 * For now `Display.rotation` and the mutable brightness and rotation fields are
 * host-side state: they are not read from or forwarded to the guest.
 */
class DisplayServiceImpl final
        : public ::android::emulation::v2::DisplayService::WithCallbackMethod_StreamDisplays<
                  ::android::emulation::v2::DisplayService::WithCallbackMethod_StreamScreenshots<
                          ::android::emulation::v2::DisplayService::Service>> {
  public:
    using Options = DisplayServiceOptions;

    struct DisplayStateEvent {
        int32_t changed_display_id = -1;
    };

    template <typename Reactor =
                      ::grpc::ServerWriteReactor<::android::emulation::v2::ListDisplaysResponse>>
    using DisplayStreamWriter = ::android::emulation::control::UniqueStateStreamWriter<
            ::android::emulation::v2::ListDisplaysResponse, DisplayStateEvent,
            ::android::base::eventing::CallbackEventSource<DisplayStateEvent>,
            /*max_queue_size=*/1, /*recycle_size=*/2, Reactor>;

    explicit DisplayServiceImpl(
            avd::AvdUniverse& universe,
            const system::SteadyClock& clock = system::SteadyClock::GetRealClock());

    explicit DisplayServiceImpl(
            display::IMultiDisplay& multi_display, sensors::PhysicalModel* physical_model = nullptr,
            Options options = {},
            const system::SteadyClock& clock = system::SteadyClock::GetRealClock());

    ~DisplayServiceImpl() override;

    DisplayServiceImpl(const DisplayServiceImpl&) = delete;
    DisplayServiceImpl& operator=(const DisplayServiceImpl&) = delete;

    ::grpc::Status ListDisplays(::grpc::ServerContext* context,
                                const ::android::emulation::v2::ListDisplaysRequest* request,
                                ::android::emulation::v2::ListDisplaysResponse* response) override;

    ::grpc::Status GetDisplay(::grpc::ServerContext* context,
                              const ::android::emulation::v2::GetDisplayRequest* request,
                              ::android::emulation::v2::Display* response) override;

    ::grpc::ServerWriteReactor<::android::emulation::v2::ListDisplaysResponse>* StreamDisplays(
            ::grpc::CallbackServerContext* context,
            const ::android::emulation::v2::StreamDisplaysRequest* request) override;

    ::grpc::Status UpdateDisplay(::grpc::ServerContext* context,
                                 const ::android::emulation::v2::UpdateDisplayRequest* request,
                                 ::android::emulation::v2::Display* response) override;

    ::grpc::Status CreateDisplay(::grpc::ServerContext* context,
                                 const ::android::emulation::v2::CreateDisplayRequest* request,
                                 ::android::emulation::v2::Display* response) override;

    ::grpc::Status DeleteDisplay(
            ::grpc::ServerContext* context,
            const ::android::emulation::v2::DeleteDisplayRequest* request,
            ::android::emulation::v2::DeleteDisplayResponse* response) override;

    ::grpc::Status GetScreenshot(::grpc::ServerContext* context,
                                 const ::android::emulation::v2::GetScreenshotRequest* request,
                                 ::android::emulation::v2::Screenshot* response) override;

    ::grpc::ServerWriteReactor<::android::emulation::v2::Screenshot>* StreamScreenshots(
            ::grpc::CallbackServerContext* context,
            const ::android::emulation::v2::StreamScreenshotsRequest* request) override;

    template <typename Reactor>
    std::unique_ptr<DisplayStreamWriter<Reactor>> StreamDisplaysForTesting(
            std::optional<int32_t> display_id = std::nullopt) {
        return std::make_unique<DisplayStreamWriter<Reactor>>(
                &display_events_,
                [this, display_id](::android::emulation::v2::ListDisplaysResponse* out) {
                    PopulateListDisplays(display_id, out);
                },
                [display_id](const DisplayStateEvent& ev) {
                    return !display_id.has_value() || ev.changed_display_id < 0 ||
                           ev.changed_display_id == *display_id;
                });
    }

  private:
    std::shared_ptr<display::IDisplay> FindAnyDisplay(int32_t display_id) const;
    std::shared_ptr<display::IDisplay> FindActiveDisplay(int32_t display_id) const;
    int32_t CountActiveDisplays() const;

    void PopulateDisplay(const display::IDisplay& disp,
                         ::android::emulation::v2::Display* out) const;
    void PopulateDisplay(const display::IDisplay& disp, const display::IDisplay::State& state,
                         ::android::emulation::v2::Display* out) const;
    void PopulateListDisplays(std::optional<int32_t> filter_display_id,
                              ::android::emulation::v2::ListDisplaysResponse* out) const;

    void OnPhysicalModelChanged();

    display::IMultiDisplay& multi_display_;
    sensors::PhysicalModel* physical_model_ = nullptr;
    const Options options_;
    ScreenshotCapturer screenshot_capturer_;
    mutable absl::Mutex topology_mutex_;
    std::atomic<display::ImageRotation> last_observed_rotation_{display::ImageRotation::kRotation0};

    ::android::base::eventing::CallbackEventSource<DisplayStateEvent> display_events_;

    std::unique_ptr<::android::base::eventing::ScopedEventCallback<display::IMultiDisplay,
                                                                   display::DisplayEvent>>
            multi_display_subscription_;
    display::IMultiDisplay::ScopedRotationSubscription physical_model_subscription_;
    std::unique_ptr<::android::base::eventing::ScopedEventCallback<
            sensors::FoldableModel::ObservablePosture, sensors::FoldablePostures>>
            posture_subscription_;
};

}  // namespace goldfish::grpc::v2
