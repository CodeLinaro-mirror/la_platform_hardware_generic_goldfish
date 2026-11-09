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

#include "display/display_service_impl.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/simple_async_grpc.h"
#include "goldfish/eventing/event_sources.h"
#include "goldfish/physics/rotation.h"
#include "goldfish/sensors/foldable.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::base::eventing::MakeScopedCallback;
using ::android::emulation::control::AbslStatusToGrpcStatus;
using ::android::emulation::v2::CreateDisplayRequest;
using ::android::emulation::v2::DeleteDisplayRequest;
using ::android::emulation::v2::DeleteDisplayResponse;
using ::android::emulation::v2::Display;
using ::android::emulation::v2::DisplayFlag;
using ::android::emulation::v2::GetDisplayRequest;
using ::android::emulation::v2::GetScreenshotRequest;
using ::android::emulation::v2::ListDisplaysRequest;
using ::android::emulation::v2::ListDisplaysResponse;
using ::android::emulation::v2::Screenshot;
using ::android::emulation::v2::StreamDisplaysRequest;
using ::android::emulation::v2::StreamScreenshotsRequest;
using ::android::emulation::v2::UpdateDisplayRequest;
using ::goldfish::display::Dimensions;
using ::goldfish::display::IDisplay;
using ::goldfish::display::ImageRotation;
using ::goldfish::display::ToImageRotation;

constexpr int32_t kPrimaryDisplayId = 0;
constexpr int32_t kMinSecondaryDisplayId = 1;
constexpr int32_t kMaxSecondaryDisplayId =
        static_cast<int32_t>(display::IMultiDisplay::kMaxDisplays) - 1;

// UpdateDisplay cannot resize displays, so this must stay false until it can.
static_assert(!kSupportsDynamicResizing,
              "Implement resizing in UpdateDisplay before advertising supports_dynamic_resizing");

struct FlagBitEntry {
    uint32_t bit;
    DisplayFlag flag;
};

constexpr FlagBitEntry kFlagBitTable[] = {
    {kAndroidFlagPublic, DisplayFlag::DISPLAY_FLAG_PUBLIC},
    {kAndroidFlagPresentation, DisplayFlag::DISPLAY_FLAG_PRESENTATION},
    {kAndroidFlagSecure, DisplayFlag::DISPLAY_FLAG_SECURE},
    {kAndroidFlagOwnContentOnly, DisplayFlag::DISPLAY_FLAG_OWN_CONTENT_ONLY},
    {kAndroidFlagAutoMirror, DisplayFlag::DISPLAY_FLAG_AUTO_MIRROR},
};

DisplayServiceImpl::Options BuildOptionsFromUniverse(const avd::AvdUniverse& universe) {
    const auto& hw = universe.Props().hw_config;
    DisplayServiceImpl::Options opts;
    if (hw.hw_lcd_density > 0) {
        opts.default_density_dpi = hw.hw_lcd_density;
    }
    return opts;
}

// True if `display_id` is in the ID range IMultiDisplay can hold.
bool IsValidDisplayId(int32_t display_id) {
    return display_id >= kPrimaryDisplayId && display_id <= kMaxSecondaryDisplayId;
}

::grpc::Status ReadOnlyPermissionDeniedStatus(const char* operation) {
    return ::grpc::Status(
            ::grpc::StatusCode::PERMISSION_DENIED,
            absl::StrFormat("Failed to execute %s: DisplayService is running in read-only access "
                            "mode; enable read-write access to mutate display state or topology.",
                            operation));
}

absl::StatusOr<uint32_t> FlagsToBitmask(const ::google::protobuf::RepeatedField<int>& proto_flags) {
    uint32_t mask = 0;
    for (const int raw_flag : proto_flags) {
        bool matched = false;
        for (const auto& entry : kFlagBitTable) {
            if (static_cast<int>(entry.flag) == raw_flag) {
                mask |= entry.bit;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Invalid DisplayFlag value %d in CreateDisplayRequest; specify only supported "
                    "non-zero DisplayFlag enum values.",
                    raw_flag));
        }
    }
    return mask;
}

void PopulateFlagsFromBitmask(uint32_t mask, Display* out) {
    out->clear_flags();
    for (const auto& entry : kFlagBitTable) {
        if ((mask & entry.bit) != 0U) {
            out->add_flags(entry.flag);
        }
    }
}

}  // namespace

DisplayServiceImpl::DisplayServiceImpl(avd::AvdUniverse& universe, const system::SteadyClock& clock)
        : DisplayServiceImpl(universe.GetMultiDisplay(), &universe.GetSensorsPhysicalModel(),
                             BuildOptionsFromUniverse(universe), clock) {}

DisplayServiceImpl::DisplayServiceImpl(display::IMultiDisplay& multi_display,
                                       sensors::PhysicalModel* physical_model, Options options,
                                       const system::SteadyClock& clock)
        : multi_display_(multi_display)
        , physical_model_(physical_model)
        , options_(options)
        , screenshot_capturer_(options.default_density_dpi, clock) {
    multi_display_subscription_ =
            MakeScopedCallback(multi_display_, [this](const display::DisplayEvent& event) {
                int32_t changed_id = -1;
                if (event.IsAddedEvent()) {
                    if (auto locked = event.Display().lock()) {
                        changed_id = static_cast<int32_t>(locked->Id());
                    }
                } else if (event.IsDeletedEvent()) {
                    changed_id = static_cast<int32_t>(event.DisplayId());
                }
                display_events_.FireEvent(DisplayStateEvent{.changed_display_id = changed_id});
            });

    if (physical_model_ != nullptr) {
        const ImageRotation initial_rotation =
                ToImageRotation(physical_model_->GetDeviceRotation().rotation);
        multi_display_.SetRotation(initial_rotation);
        last_observed_rotation_.store(initial_rotation, std::memory_order_release);

        physical_model_subscription_ = MakeScopedCallback(
                *physical_model_, [this](const sensors::PhysicalModelChangeEvent& /*event*/) {
                    OnPhysicalModelChanged();
                });

        if (auto* posture_listener = physical_model_->GetPostureListener();
            posture_listener != nullptr) {
            multi_display_.SetFolded(physical_model_->FoldableIsFolded());
            posture_subscription_ = MakeScopedCallback(
                    *posture_listener, [this](const sensors::FoldablePostures& posture) {
                        multi_display_.SetFolded(posture == sensors::FoldablePostures::kClosed);
                        display_events_.FireEvent(DisplayStateEvent{.changed_display_id = -1});
                    });
        }
    }
}

DisplayServiceImpl::~DisplayServiceImpl() {
    posture_subscription_.reset();
    physical_model_subscription_.reset();
    multi_display_subscription_.reset();
}

void DisplayServiceImpl::OnPhysicalModelChanged() {
    if (physical_model_ == nullptr) {
        return;
    }
    const ImageRotation sensor_rotation =
            ToImageRotation(physical_model_->GetDeviceRotation().rotation);
    const ImageRotation prev =
            last_observed_rotation_.exchange(sensor_rotation, std::memory_order_acq_rel);
    if (sensor_rotation != prev) {
        multi_display_.SetRotation(sensor_rotation);
        display_events_.FireEvent(DisplayStateEvent{.changed_display_id = -1});
    }
}

std::shared_ptr<IDisplay> DisplayServiceImpl::FindAnyDisplay(int32_t display_id) const {
    if (!IsValidDisplayId(display_id)) {
        return nullptr;
    }
    auto res = multi_display_.GetDisplay(static_cast<display::DisplayId>(display_id));
    if (!res.ok()) {
        return nullptr;
    }
    return res->lock();
}

std::shared_ptr<IDisplay> DisplayServiceImpl::FindActiveDisplay(int32_t display_id) const {
    if (!IsValidDisplayId(display_id)) {
        return nullptr;
    }
    if (!multi_display_.IsActive(static_cast<display::DisplayId>(display_id))) {
        return nullptr;
    }
    return FindAnyDisplay(display_id);
}

int32_t DisplayServiceImpl::CountActiveDisplays() const {
    int32_t count = 0;
    for (const auto& weak_disp : multi_display_.Displays()) {
        if (weak_disp.lock() != nullptr) {
            ++count;
        }
    }
    return count;
}

void DisplayServiceImpl::PopulateDisplay(const IDisplay& disp, Display* out) const {
    PopulateDisplay(disp, disp.GetState(), out);
}

void DisplayServiceImpl::PopulateDisplay(const IDisplay& disp, const IDisplay::State& state,
                                         Display* out) const {
    const int32_t id = static_cast<int32_t>(disp.Id());
    const Dimensions dims = disp.GetDimensions();
    const int32_t dpi =
            disp.Dpi() > 0 ? static_cast<int32_t>(disp.Dpi()) : options_.default_density_dpi;

    out->Clear();
    out->set_display_id(id);
    out->set_primary(id == kPrimaryDisplayId);
    out->set_width(static_cast<int32_t>(dims.width));
    out->set_height(static_cast<int32_t>(dims.height));
    out->set_density_dpi(dpi);
    // For now these are host-side values. `rotation` is the rotation the host applies to the
    // display frames, not the guest WindowManager rotation that the proto describes, and the
    // brightness and rotation settings below are not forwarded to the guest.
    out->set_rotation(ToProtoRotation(state.EffectiveRotation()));
    out->set_brightness_percent(state.brightness_percent);
    out->set_auto_brightness_enabled(state.auto_brightness_enabled);
    out->set_auto_rotation_enabled(state.auto_rotation_enabled);
    out->set_user_rotation(ToProtoRotation(state.user_rotation));
    PopulateFlagsFromBitmask(disp.Flags(), out);
}

void DisplayServiceImpl::PopulateListDisplays(std::optional<int32_t> filter_display_id,
                                              ListDisplaysResponse* out) const {
    out->clear_displays();

    std::vector<std::shared_ptr<IDisplay>> active;
    for (const auto& weak_disp : multi_display_.Displays()) {
        if (auto disp = weak_disp.lock()) {
            if (!filter_display_id.has_value() ||
                static_cast<int32_t>(disp->Id()) == *filter_display_id) {
                active.push_back(std::move(disp));
            }
        }
    }
    std::sort(active.begin(), active.end(),
              [](const std::shared_ptr<IDisplay>& a, const std::shared_ptr<IDisplay>& b) {
                  return a->Id() < b->Id();
              });

    for (const auto& disp : active) {
        PopulateDisplay(*disp, out->add_displays());
    }
}

::grpc::Status DisplayServiceImpl::ListDisplays(::grpc::ServerContext* /*context*/,
                                                const ListDisplaysRequest* /*request*/,
                                                ListDisplaysResponse* response) {
    PopulateListDisplays(std::nullopt, response);
    return ::grpc::Status::OK;
}

::grpc::Status DisplayServiceImpl::GetDisplay(::grpc::ServerContext* /*context*/,
                                              const GetDisplayRequest* request, Display* response) {
    const int32_t id = request->display_id();
    auto disp = FindActiveDisplay(id);
    if (!disp) {
        return ::grpc::Status(
                ::grpc::StatusCode::NOT_FOUND,
                absl::StrFormat("Failed to get display %d: no active display exists with ID %d; "
                                "call ListDisplays to inspect active display IDs.",
                                id, id));
    }
    PopulateDisplay(*disp, response);
    return ::grpc::Status::OK;
}

::grpc::ServerWriteReactor<ListDisplaysResponse>* DisplayServiceImpl::StreamDisplays(
        ::grpc::CallbackServerContext* /*context*/, const StreamDisplaysRequest* request) {
    std::optional<int32_t> filter_id;
    if (request->has_display_id()) {
        if (request->display_id() < 0) {
            return new ::ErrorServerWriter<ListDisplaysResponse>(::grpc::Status(
                    ::grpc::StatusCode::INVALID_ARGUMENT,
                    absl::StrFormat("Invalid display_id %d in StreamDisplaysRequest; specify a "
                                    "display_id >= 0 or omit display_id to watch all displays.",
                                    request->display_id())));
        }
        filter_id = request->display_id();
    }

    return new DisplayStreamWriter<::grpc::ServerWriteReactor<ListDisplaysResponse>>(
            &display_events_,
            [this, filter_id](ListDisplaysResponse* out) { PopulateListDisplays(filter_id, out); },
            [filter_id](const DisplayStateEvent& ev) {
                return !filter_id.has_value() || ev.changed_display_id < 0 ||
                       ev.changed_display_id == *filter_id;
            });
}

::grpc::Status DisplayServiceImpl::UpdateDisplay(::grpc::ServerContext* /*context*/,
                                                 const UpdateDisplayRequest* request,
                                                 Display* response) {
    if (options_.read_only) {
        return ReadOnlyPermissionDeniedStatus("UpdateDisplay");
    }

    const int32_t id = request->display_id();
    auto disp = FindActiveDisplay(id);
    if (!disp) {
        return ::grpc::Status(
                ::grpc::StatusCode::NOT_FOUND,
                absl::StrFormat("Failed to update display %d: no active display exists with ID %d; "
                                "call ListDisplays to inspect active display IDs.",
                                id, id));
    }

    if (request->has_width() && request->width() <= 0) {
        return ::grpc::Status(
                ::grpc::StatusCode::INVALID_ARGUMENT,
                absl::StrFormat("Invalid width %d in UpdateDisplayRequest for display %d; specify "
                                "a positive width > 0 or omit width.",
                                request->width(), id));
    }
    if (request->has_height() && request->height() <= 0) {
        return ::grpc::Status(
                ::grpc::StatusCode::INVALID_ARGUMENT,
                absl::StrFormat("Invalid height %d in UpdateDisplayRequest for display %d; specify "
                                "a positive height > 0 or omit height.",
                                request->height(), id));
    }
    if (request->has_density_dpi() && request->density_dpi() <= 0) {
        return ::grpc::Status(
                ::grpc::StatusCode::INVALID_ARGUMENT,
                absl::StrFormat("Invalid density_dpi %d in UpdateDisplayRequest for display %d; "
                                "specify a positive density_dpi > 0 or omit density_dpi.",
                                request->density_dpi(), id));
    }
    if (request->has_brightness_percent() &&
        (!std::isfinite(request->brightness_percent()) || request->brightness_percent() < 0.0F ||
         request->brightness_percent() > 100.0F)) {
        return ::grpc::Status(
                ::grpc::StatusCode::INVALID_ARGUMENT,
                absl::StrFormat("Invalid brightness_percent %f in UpdateDisplayRequest for "
                                "display %d; specify a finite percentage in [0.0, 100.0].",
                                static_cast<double>(request->brightness_percent()), id));
    }

    std::optional<ImageRotation> parsed_user_rotation;
    if (request->has_user_rotation()) {
        auto rot = FromProtoRotation(request->user_rotation());
        if (!rot.ok()) {
            return AbslStatusToGrpcStatus(rot.status());
        }
        parsed_user_rotation = *rot;
    }

    if (request->has_width() || request->has_height() || request->has_density_dpi()) {
        const char* next_step =
                id == kPrimaryDisplayId
                        ? "configure the primary display geometry in the AVD hardware profile "
                          "before boot"
                        : "call DeleteDisplay followed by CreateDisplay to replace a virtual "
                          "secondary display";
        return ::grpc::Status(
                ::grpc::StatusCode::FAILED_PRECONDITION,
                absl::StrFormat("Failed to resize display %d: this emulator does not support "
                                "dynamic resizing (supports_dynamic_resizing is false); %s.",
                                id, next_step));
    }

    IDisplay::StateUpdate state_update;
    if (request->has_brightness_percent()) {
        state_update.brightness_percent = request->brightness_percent();
    }
    if (request->has_auto_brightness_enabled()) {
        state_update.auto_brightness_enabled = request->auto_brightness_enabled();
    }
    if (parsed_user_rotation.has_value()) {
        state_update.user_rotation = *parsed_user_rotation;
    }
    if (request->has_auto_rotation_enabled()) {
        // No device rotation re-read: IMultiDisplay::SetRotation() keeps it current while the
        // rotation is locked, and a re-read here could overwrite a newer rotation.
        state_update.auto_rotation_enabled = request->auto_rotation_enabled();
    }

    const IDisplay::State final_state = disp->UpdateState(state_update);
    PopulateDisplay(*disp, final_state, response);
    display_events_.FireEvent(DisplayStateEvent{.changed_display_id = id});
    return ::grpc::Status::OK;
}

::grpc::Status DisplayServiceImpl::CreateDisplay(::grpc::ServerContext* /*context*/,
                                                 const CreateDisplayRequest* request,
                                                 Display* response) {
    if (options_.read_only) {
        return ReadOnlyPermissionDeniedStatus("CreateDisplay");
    }

    if (!options_.supports_virtual_display_creation) {
        return ::grpc::Status(
                ::grpc::StatusCode::FAILED_PRECONDITION,
                "Failed to create secondary virtual display: supports_virtual_display_creation is "
                "disabled on this device; query CapabilitiesService.GetDeviceCapabilities before "
                "calling CreateDisplay.");
    }

    if (request->width() <= 0 || request->height() <= 0 || request->density_dpi() <= 0) {
        return ::grpc::Status(
                ::grpc::StatusCode::INVALID_ARGUMENT,
                absl::StrFormat("Invalid dimensions or density in CreateDisplayRequest "
                                "(width=%d, height=%d, density_dpi=%d); specify values > 0 for "
                                "width, height, and density_dpi.",
                                request->width(), request->height(), request->density_dpi()));
    }

    auto flags_mask = FlagsToBitmask(request->flags());
    if (!flags_mask.ok()) {
        return AbslStatusToGrpcStatus(flags_mask.status());
    }

    int32_t target_id = request->display_id();
    if (target_id < 0 || target_id > kMaxSecondaryDisplayId) {
        return ::grpc::Status(
                ::grpc::StatusCode::INVALID_ARGUMENT,
                absl::StrFormat("Invalid display_id %d in CreateDisplayRequest; specify 0 to "
                                "auto-allocate or a secondary display ID in [%d, %d].",
                                target_id, kMinSecondaryDisplayId, kMaxSecondaryDisplayId));
    }

    std::shared_ptr<IDisplay> disp;
    {
        const absl::MutexLock lock(topology_mutex_);
        if (target_id > 0 && FindAnyDisplay(target_id) != nullptr) {
            return ::grpc::Status(
                    ::grpc::StatusCode::ALREADY_EXISTS,
                    absl::StrFormat("Failed to create display %d: a display with ID %d already "
                                    "exists; omit display_id (0) to auto-allocate a free ID or "
                                    "delete the existing virtual display first.",
                                    target_id, target_id));
        }

        const int32_t active_count = CountActiveDisplays();
        if (active_count >= options_.max_supported_display_count) {
            return ::grpc::Status(
                    ::grpc::StatusCode::RESOURCE_EXHAUSTED,
                    absl::StrFormat("Failed to create secondary display: active display count (%d) "
                                    "has reached max_supported_display_count (%d); call "
                                    "DeleteDisplay on an existing virtual display first.",
                                    active_count, options_.max_supported_display_count));
        }

        if (target_id == 0) {
            int32_t next_id = -1;
            for (int32_t candidate = kMinSecondaryDisplayId; candidate <= kMaxSecondaryDisplayId;
                 ++candidate) {
                if (FindAnyDisplay(candidate) == nullptr) {
                    next_id = candidate;
                    break;
                }
            }
            if (next_id < kMinSecondaryDisplayId) {
                return ::grpc::Status(
                        ::grpc::StatusCode::RESOURCE_EXHAUSTED,
                        absl::StrFormat("Failed to create secondary display: all secondary display "
                                        "IDs in [%d, %d] are occupied; call DeleteDisplay on an "
                                        "existing virtual display first.",
                                        kMinSecondaryDisplayId, kMaxSecondaryDisplayId));
            }
            target_id = next_id;
        }

        auto created = multi_display_.CreateDisplay(
                static_cast<display::DisplayId>(target_id), static_cast<uint32_t>(request->width()),
                static_cast<uint32_t>(request->height()),
                static_cast<uint32_t>(request->density_dpi()), *flags_mask);
        if (!created.ok()) {
            return AbslStatusToGrpcStatus(created.status());
        }

        disp = created->lock();
        if (!disp) {
            return ::grpc::Status(
                    ::grpc::StatusCode::INTERNAL,
                    absl::StrFormat("Failed to create display %d: created display handle expired "
                                    "immediately; retry CreateDisplay.",
                                    target_id));
        }
    }

    PopulateDisplay(*disp, response);
    display_events_.FireEvent(DisplayStateEvent{.changed_display_id = target_id});
    return ::grpc::Status::OK;
}

::grpc::Status DisplayServiceImpl::DeleteDisplay(::grpc::ServerContext* /*context*/,
                                                 const DeleteDisplayRequest* request,
                                                 DeleteDisplayResponse* /*response*/) {
    if (options_.read_only) {
        return ReadOnlyPermissionDeniedStatus("DeleteDisplay");
    }

    const int32_t id = request->display_id();
    if (id <= kPrimaryDisplayId) {
        return ::grpc::Status(
                ::grpc::StatusCode::INVALID_ARGUMENT,
                absl::StrFormat("Failed to delete display %d: primary display 0 cannot be deleted "
                                "and display_id must be >= %d; specify an active virtual secondary "
                                "display ID.",
                                id, kMinSecondaryDisplayId));
    }

    {
        const absl::MutexLock lock(topology_mutex_);
        auto disp = FindAnyDisplay(id);
        if (!disp) {
            return ::grpc::Status(
                    ::grpc::StatusCode::NOT_FOUND,
                    absl::StrFormat("Failed to delete display %d: no secondary display exists "
                                    "with ID %d; call ListDisplays to inspect active displays.",
                                    id, id));
        }
        if (disp->GetConsole() != nullptr) {
            return ::grpc::Status(
                    ::grpc::StatusCode::INVALID_ARGUMENT,
                    absl::StrFormat("Failed to delete display %d: display %d is a built-in "
                                    "hardware display and cannot be removed; only dynamically "
                                    "created virtual secondary displays can be deleted.",
                                    id, id));
        }
        if (!multi_display_.IsActive(static_cast<display::DisplayId>(id))) {
            return ::grpc::Status(
                    ::grpc::StatusCode::NOT_FOUND,
                    absl::StrFormat("Failed to delete display %d: no active virtual secondary "
                                    "display exists with ID %d; call ListDisplays to inspect "
                                    "active displays.",
                                    id, id));
        }

        const absl::Status status =
                multi_display_.EraseDisplay(static_cast<display::DisplayId>(id));
        if (!status.ok()) {
            return AbslStatusToGrpcStatus(status);
        }
    }

    display_events_.FireEvent(DisplayStateEvent{.changed_display_id = id});
    return ::grpc::Status::OK;
}

::grpc::Status DisplayServiceImpl::GetScreenshot(::grpc::ServerContext* /*context*/,
                                                 const GetScreenshotRequest* request,
                                                 Screenshot* response) {
    const int32_t display_id = request->display_id();
    auto disp = FindActiveDisplay(display_id);
    if (!disp) {
        return ::grpc::Status(
                ::grpc::StatusCode::NOT_FOUND,
                absl::StrFormat("Failed to capture screenshot of display %d: no active display "
                                "exists with ID %d; call ListDisplays to inspect active displays.",
                                display_id, display_id));
    }
    return screenshot_capturer_.CaptureScreenshot(*disp, *request, response);
}

::grpc::ServerWriteReactor<Screenshot>* DisplayServiceImpl::StreamScreenshots(
        ::grpc::CallbackServerContext* /*context*/, const StreamScreenshotsRequest* request) {
    const int32_t display_id = request->display_id();
    auto disp = FindActiveDisplay(display_id);
    if (!disp) {
        return new ::ErrorServerWriter<Screenshot>(::grpc::Status(
                ::grpc::StatusCode::NOT_FOUND,
                absl::StrFormat("Failed to stream screenshots of display %d: no active display "
                                "exists with ID %d; call ListDisplays to inspect active displays.",
                                display_id, display_id)));
    }
    return screenshot_capturer_.StreamScreenshots(std::move(disp), *request);
}

}  // namespace goldfish::grpc::v2
