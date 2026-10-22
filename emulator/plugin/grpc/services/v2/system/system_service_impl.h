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

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "android/emulation/control/grpc_event_stream_support.h"
#include "android/goldfish/vm_interface.h"
#include "goldfish/avd_universe/clipboard/clipboard_data.h"
#include "goldfish/avd_universe/fingerprint/fingerprint_sensor.h"
#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"
#include "goldfish/avd_universe/guest_status/guest_status.h"
#include "goldfish/eventing/event_sources.h"
#include "system_service.grpc.pb.h"

namespace goldfish::grpc::v2 {

using SystemServiceBase =
        ::android::emulation::v2::SystemService::WithCallbackMethod_StreamSystemState<
                ::android::emulation::v2::SystemService::WithCallbackMethod_StreamClipboardState<
                        ::android::emulation::v2::SystemService::Service>>;

/**
 * @brief Implements the AEMU v2 SystemService.
 *
 * Provides real-time telemetry on guest OS lifecycle (boot completed, uptime,
 * heartbeat), ADB connection readiness, in-band OS reboot, guest clipboard
 * synchronization, and biometric fingerprint injection.
 *
 * ### Streaming Behavior:
 * - `StreamSystemState` pushes an initial snapshot immediately upon subscription,
 *   and reactively pushes updates when the guest OS completes booting.
 * - `StreamClipboardState` pushes the current `ClipboardState` immediately upon
 *   subscription, and reactively pushes updates whenever guest apps copy new
 *   clipboard data.
 *
 * ### Reboot and Reset Semantics:
 * - When QEMU is reset (e.g. via RebootDevice() or the UI reset button),
 *   GuestStatus::Reset() clears the boot completed timestamp in memory.
 * - Reset initiation does not currently broadcast an event on GrpcNotificationEventSource;
 *   therefore, the stream will not emit an intermediate BOOTING state when reset begins.
 * - Once the guest OS finishes rebooting, a `booted` notification is emitted on
 *   the notification channel, causing this service to push an updated BOOT_COMPLETED
 *   state with the new boot duration.
 * - In-guest software reboots (e.g. `adb reboot`) bypass QEMU hardware reset and do
 *   not re-trigger boot notification unless a full hardware reset occurs.
 *
 * ### Thread Safety:
 * - Thread-safe across concurrent gRPC threads and HAL device callback threads.
 *   Clipboard state is synchronized under `clipboard_mutex_`, and external
 *   observers are notified outside the lock.
 *
 * ### Ownership & Lifetimes:
 * - Holds non-owning references/pointers to `GuestStatus`,
 *   `GrpcNotificationEventSource`, `VmOperations`, `ClipboardChannel`, and
 *   `ObservableFingerprintSensor`, all of which must outlive this service
 *   instance. Deregisters its `ClipboardChannel::guest_to_host` callback on
 *   destruction.
 */
class SystemServiceImpl final : public SystemServiceBase {
  public:
    static constexpr absl::Duration kDefaultFingerprintHoldDuration = absl::Milliseconds(100);

    using GuestStatus = ::goldfish::avd_universe::guest_status::GuestStatus;
    using GrpcNotificationEventSource = ::goldfish::avd_universe::grpc::GrpcNotificationEventSource;
    using GrpcNotification = ::goldfish::avd_universe::grpc::GrpcNotification;
    using VmOperations = ::android::goldfish::VmOperations;
    using ClipboardChannel = ::goldfish::avd_universe::clipboard::ClipboardChannel;
    using ClipboardData = ::goldfish::avd_universe::clipboard::ClipboardData;
    using ObservableClipboardData = ::goldfish::avd_universe::clipboard::ObservableClipboardData;
    using ObservableFingerprintSensor =
            ::goldfish::avd_universe::fingerprint::ObservableFingerprintSensor;
    using ClipboardEventSource = ::android::base::eventing::CallbackEventSource<ClipboardData>;

    using SystemState = ::android::emulation::v2::SystemState;
    using GetSystemStateRequest = ::android::emulation::v2::GetSystemStateRequest;
    using StreamSystemStateRequest = ::android::emulation::v2::StreamSystemStateRequest;
    using RebootDeviceRequest = ::android::emulation::v2::RebootDeviceRequest;
    using RebootDeviceResponse = ::android::emulation::v2::RebootDeviceResponse;
    using ClipboardState = ::android::emulation::v2::ClipboardState;
    using GetClipboardStateRequest = ::android::emulation::v2::GetClipboardStateRequest;
    using StreamClipboardStateRequest = ::android::emulation::v2::StreamClipboardStateRequest;
    using UpdateClipboardStateRequest = ::android::emulation::v2::UpdateClipboardStateRequest;
    using InjectFingerprintRequest = ::android::emulation::v2::InjectFingerprintRequest;
    using InjectFingerprintResponse = ::android::emulation::v2::InjectFingerprintResponse;

    template <typename Reactor = ::grpc::ServerWriteReactor<SystemState>>
    using SystemStateWriter = ::android::emulation::control::StateStreamWriter<
            SystemState, GrpcNotification, GrpcNotificationEventSource, 0, 0, Reactor>;

    template <typename Reactor = ::grpc::ServerWriteReactor<ClipboardState>>
    using ClipboardStateWriter =
            ::android::emulation::control::StateStreamWriter<ClipboardState, ClipboardData,
                                                             ClipboardEventSource, 0, 0, Reactor>;

    SystemServiceImpl(GuestStatus& guest_status, GrpcNotificationEventSource* notification_source,
                      VmOperations* vm_operations, ClipboardChannel* clipboard_channel,
                      ObservableFingerprintSensor* fingerprint_sensor,
                      absl::Duration fingerprint_hold_duration = kDefaultFingerprintHoldDuration);

    ~SystemServiceImpl() override;

    ::grpc::Status GetSystemState(::grpc::ServerContext* context,
                                  const GetSystemStateRequest* request,
                                  SystemState* reply) override;

    /**
     * @brief Constructs a StateStreamWriter for streaming SystemState messages.
     *
     * Subscribes to the GrpcNotificationEventSource observable. The incoming
     * notifications are filtered to `notif.has_booted()` to ignore unrelated
     * emulator notifications (such as camera, fold/posture, screen brightness)
     * and only stream updates when the guest OS boot state completes.
     *
     * TODO(jansene): Figure out if we want to propagate heartbeats down the stream.
     */
    template <typename Reactor = ::grpc::ServerWriteReactor<SystemState>>
    auto* CreateSystemStateWriter() {
        DCHECK(notification_source_);
        return new SystemStateWriter<Reactor>(
                notification_source_, [this](SystemState* state) { PopulateSystemState(state); },
                [](const GrpcNotification& notif) { return notif.has_booted(); });
    }

    ::grpc::ServerWriteReactor<SystemState>* StreamSystemState(
            ::grpc::CallbackServerContext* context,
            const StreamSystemStateRequest* request) override;

    ::grpc::Status RebootDevice(::grpc::ServerContext* context, const RebootDeviceRequest* request,
                                RebootDeviceResponse* reply) override;

    ::grpc::Status GetClipboardState(::grpc::ServerContext* context,
                                     const GetClipboardStateRequest* request,
                                     ClipboardState* reply) override;

    /**
     * @brief Constructs a StateStreamWriter for streaming ClipboardState messages.
     */
    template <typename Reactor = ::grpc::ServerWriteReactor<ClipboardState>>
    auto* CreateClipboardStateWriter() {
        DCHECK(clipboard_channel_);
        return new ClipboardStateWriter<Reactor>(&clipboard_events_, [this](ClipboardState* state) {
            PopulateClipboardState(state);
        });
    }

    ::grpc::ServerWriteReactor<ClipboardState>* StreamClipboardState(
            ::grpc::CallbackServerContext* context,
            const StreamClipboardStateRequest* request) override;

    ::grpc::Status UpdateClipboardState(::grpc::ServerContext* context,
                                        const UpdateClipboardStateRequest* request,
                                        ClipboardState* reply) override;

    ::grpc::Status InjectFingerprint(::grpc::ServerContext* context,
                                     const InjectFingerprintRequest* request,
                                     InjectFingerprintResponse* reply) override;

  private:
    void PopulateSystemState(SystemState* state);
    void PopulateClipboardState(ClipboardState* state) ABSL_LOCKS_EXCLUDED(clipboard_mutex_);
    void OnGuestClipboardUpdated(const ClipboardData& clip) ABSL_LOCKS_EXCLUDED(clipboard_mutex_);

    GuestStatus& guest_status_;
    GrpcNotificationEventSource* const notification_source_;
    VmOperations* const vm_operations_;
    ClipboardChannel* const clipboard_channel_;
    ObservableFingerprintSensor* const fingerprint_sensor_;
    const absl::Duration fingerprint_hold_duration_;

    mutable absl::Mutex clipboard_mutex_;
    ClipboardState current_clipboard_ ABSL_GUARDED_BY(clipboard_mutex_);
    ClipboardEventSource clipboard_events_;
    ObservableClipboardData::CallbackId guest_clipboard_callback_id_{
        ObservableClipboardData::kInvalidCallbackId};
};

}  // namespace goldfish::grpc::v2
