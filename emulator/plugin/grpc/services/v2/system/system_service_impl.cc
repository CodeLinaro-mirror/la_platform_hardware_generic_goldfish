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

#include "system_service_impl.h"

#include <functional>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/base/system.h"
#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/grpc_event_stream_support.h"

namespace goldfish::grpc::v2 {

using ::android::base::System;
using ::android::emulation::control::AbslStatusToGrpcStatus;
using ::android::emulation::control::StateStreamWriter;
using ::android::emulation::v2::ClipboardState;
using ::android::emulation::v2::GetClipboardStateRequest;
using ::android::emulation::v2::GetSystemStateRequest;
using ::android::emulation::v2::InjectFingerprintRequest;
using ::android::emulation::v2::InjectFingerprintResponse;
using ::android::emulation::v2::RebootDeviceRequest;
using ::android::emulation::v2::RebootDeviceResponse;
using ::android::emulation::v2::StreamClipboardStateRequest;
using ::android::emulation::v2::StreamSystemStateRequest;
using ::android::emulation::v2::SystemState;
using ::android::emulation::v2::UpdateClipboardStateRequest;
using ::android::goldfish::VmOperations;
using ::goldfish::avd_universe::clipboard::ClipboardChannel;
using ::goldfish::avd_universe::clipboard::ClipboardData;
using ::goldfish::avd_universe::fingerprint::kReleaseEvent;
using ::goldfish::avd_universe::fingerprint::ObservableFingerprintSensor;
using ::goldfish::avd_universe::grpc::GrpcNotification;
using ::goldfish::avd_universe::grpc::GrpcNotificationEventSource;
using ::goldfish::avd_universe::guest_status::GuestStatus;
using ::grpc::CallbackServerContext;
using ::grpc::ServerContext;
using ::grpc::ServerWriteReactor;
using ::grpc::Status;

SystemServiceImpl::SystemServiceImpl(GuestStatus& guest_status,
                                     GrpcNotificationEventSource* notification_source,
                                     VmOperations* vm_operations,
                                     ClipboardChannel* clipboard_channel,
                                     ObservableFingerprintSensor* fingerprint_sensor,
                                     absl::Duration fingerprint_hold_duration)
        : guest_status_(guest_status)
        , notification_source_(notification_source)
        , vm_operations_(vm_operations)
        , clipboard_channel_(clipboard_channel)
        , fingerprint_sensor_(fingerprint_sensor)
        , fingerprint_hold_duration_(fingerprint_hold_duration) {
    DCHECK(notification_source_);
    DCHECK(vm_operations_);
    DCHECK(clipboard_channel_);
    DCHECK(fingerprint_sensor_);
    OnGuestClipboardUpdated(clipboard_channel_->guest_to_host.GetValue());
    guest_clipboard_callback_id_ = clipboard_channel_->guest_to_host.AddCallback(
            [this](const ClipboardData& clip) { OnGuestClipboardUpdated(clip); });
}

SystemServiceImpl::~SystemServiceImpl() {
    if (guest_clipboard_callback_id_ != ObservableClipboardData::kInvalidCallbackId) {
        clipboard_channel_->guest_to_host.RemoveCallback(guest_clipboard_callback_id_);
    }
}

void SystemServiceImpl::PopulateSystemState(SystemState* state) {
    bool booted = guest_status_.IsBootCompleted();
    state->set_lifecycle_state(booted ? SystemState::BOOT_COMPLETED : SystemState::BOOTING);
    state->set_connection_state(booted ? SystemState::CONNECTED : SystemState::DISCONNECTED);
    state->set_lock_state(SystemState::UNLOCKED);

    auto* sys = System::Get();
    if (sys) {
        state->set_uptime_ms(sys->GetProcessTimes().wall_clock_ms);
    }

    if (booted) {
        state->set_boot_completed_duration_ms(
                absl::ToInt64Milliseconds(guest_status_.GetBootCompleteDuration()));
    } else {
        state->set_boot_completed_duration_ms(0);
    }

    state->set_heartbeat_counter(guest_status_.GetHeartbeatCounter());
}

void SystemServiceImpl::PopulateClipboardState(ClipboardState* state) {
    absl::MutexLock clipboard_lock(&clipboard_mutex_);
    *state = current_clipboard_;
}

void SystemServiceImpl::OnGuestClipboardUpdated(const ClipboardData& clip) {
    {
        absl::MutexLock clipboard_lock(&clipboard_mutex_);
        current_clipboard_.Clear();
        current_clipboard_.set_text(clip.contents);
        if (!clip.contents.empty()) {
            current_clipboard_.set_mime_type("text/plain");
        }
    }
    clipboard_events_.FireEvent(clip);
}

Status SystemServiceImpl::GetSystemState(ServerContext* /*context*/,
                                         const GetSystemStateRequest* /*request*/,
                                         SystemState* reply) {
    PopulateSystemState(reply);
    return Status::OK;
}

ServerWriteReactor<SystemState>* SystemServiceImpl::StreamSystemState(
        CallbackServerContext* /*context*/, const StreamSystemStateRequest* /*request*/) {
    return CreateSystemStateWriter();
}

Status SystemServiceImpl::RebootDevice(ServerContext* /*context*/,
                                       const RebootDeviceRequest* /*request*/,
                                       RebootDeviceResponse* /*reply*/) {
    DCHECK(vm_operations_);
    return AbslStatusToGrpcStatus(vm_operations_->Reset());
}

Status SystemServiceImpl::GetClipboardState(ServerContext* /*context*/,
                                            const GetClipboardStateRequest* /*request*/,
                                            ClipboardState* reply) {
    DCHECK(clipboard_channel_);
    PopulateClipboardState(reply);
    return Status::OK;
}

ServerWriteReactor<ClipboardState>* SystemServiceImpl::StreamClipboardState(
        CallbackServerContext* /*context*/, const StreamClipboardStateRequest* /*request*/) {
    return CreateClipboardStateWriter();
}

Status SystemServiceImpl::UpdateClipboardState(ServerContext* /*context*/,
                                               const UpdateClipboardStateRequest* request,
                                               ClipboardState* reply) {
    DCHECK(clipboard_channel_);
    {
        absl::MutexLock clipboard_lock(&clipboard_mutex_);
        current_clipboard_.Clear();
        current_clipboard_.set_text(request->text());
        if (!request->mime_type().empty()) {
            current_clipboard_.set_mime_type(request->mime_type());
        } else if (!request->text().empty()) {
            current_clipboard_.set_mime_type("text/plain");
        }
        if (request->has_binary_data()) {
            current_clipboard_.set_binary_data(request->binary_data());
        }
        *reply = current_clipboard_;
    }
    clipboard_channel_->host_to_guest.SetValue({.contents = request->text()});
    return Status::OK;
}

Status SystemServiceImpl::InjectFingerprint(ServerContext* /*context*/,
                                            const InjectFingerprintRequest* request,
                                            InjectFingerprintResponse* /*reply*/) {
    DCHECK(fingerprint_sensor_);
    if (!request->has_touching()) {
        fingerprint_sensor_->SetValue(request->finger_id());
        if (fingerprint_hold_duration_ > absl::ZeroDuration()) {
            absl::SleepFor(fingerprint_hold_duration_);
        }
        fingerprint_sensor_->SetValue(kReleaseEvent);
        return Status::OK;
    }
    if (request->touching()) {
        fingerprint_sensor_->SetValue(request->finger_id());
    } else {
        fingerprint_sensor_->SetValue(kReleaseEvent);
    }
    return Status::OK;
}

}  // namespace goldfish::grpc::v2
