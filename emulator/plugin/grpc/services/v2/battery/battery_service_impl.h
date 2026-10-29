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

#include <cstddef>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "android/emulation/control/grpc_event_stream_support.h"
#include "battery_service.grpc.pb.h"
#include "goldfish/avd_universe/battery/battery_state.h"
#include "goldfish/eventing/event_sources.h"

namespace goldfish::grpc::v2 {

using BatteryServiceBase =
        ::android::emulation::v2::BatteryService::WithCallbackMethod_StreamBatteryState<
                ::android::emulation::v2::BatteryService::Service>;

/**
 * @brief Implements the AEMU v2 BatteryService gRPC interface.
 *
 * Observes and mutates battery charge level, power supply type, charging state,
 * and thermal health over the shared `ObservableBattery` state.
 *
 * ### Streaming Behavior:
 * - `StreamBatteryState` writes an initial snapshot immediately upon subscription,
 *   and reactively writes subsequent updates whenever any battery property changes.
 *   Rapid intermediate updates are coalesced to the latest state for slow clients.
 *
 * ### Thread Safety:
 * - Thread-safe across concurrent gRPC calls and `ObservableBattery` updates.
 *   Cached battery state is synchronized under `battery_mutex_`, and stream
 *   observers are notified via `battery_events_` outside the lock.
 *
 * ### Ownership & Lifetimes:
 * - Holds a non-owning reference to `goldfish::avd_universe::battery::ObservableBattery`,
 *   which must outlive this service instance. Deregisters its `ObservableBattery`
 *   callback on destruction.
 */
class BatteryServiceImpl final : public BatteryServiceBase {
  public:
    using ObservableBattery = ::goldfish::avd_universe::battery::ObservableBattery;
    using Battery = ::goldfish::avd_universe::battery::Battery;
    using BatteryEventSource = ::android::base::eventing::CallbackEventSource<Battery>;

    using BatteryState = ::android::emulation::v2::BatteryState;
    using GetBatteryStateRequest = ::android::emulation::v2::GetBatteryStateRequest;
    using StreamBatteryStateRequest = ::android::emulation::v2::StreamBatteryStateRequest;
    using UpdateBatteryStateRequest = ::android::emulation::v2::UpdateBatteryStateRequest;
    using ChargerSource = ::android::emulation::v2::ChargerSource;

    static constexpr size_t kMaxPendingStreamUpdates = 1;

    struct Options {
        bool has_battery{true};
        bool read_only{false};
    };

    template <typename Reactor = ::grpc::ServerWriteReactor<BatteryState>>
    using BatteryStateWriter = ::android::emulation::control::StateStreamWriter<
            BatteryState, Battery, BatteryEventSource, kMaxPendingStreamUpdates, 0, Reactor>;

    explicit BatteryServiceImpl(ObservableBattery& observable_battery, Options options);
    explicit BatteryServiceImpl(ObservableBattery& observable_battery)
            : BatteryServiceImpl(observable_battery, Options{}) {}
    ~BatteryServiceImpl() override;

    ::grpc::Status GetBatteryState(::grpc::ServerContext* context,
                                   const GetBatteryStateRequest* request,
                                   BatteryState* reply) override;

    /**
     * @brief Constructs a StateStreamWriter for streaming BatteryState messages.
     */
    template <typename Reactor = ::grpc::ServerWriteReactor<BatteryState>>
    auto* CreateBatteryStateWriter() {
        return new BatteryStateWriter<Reactor>(
                &battery_events_, [this](BatteryState* state) { PopulateBatteryState(state); });
    }

    ::grpc::ServerWriteReactor<BatteryState>* StreamBatteryState(
            ::grpc::CallbackServerContext* context,
            const StreamBatteryStateRequest* request) override;

    ::grpc::Status UpdateBatteryState(::grpc::ServerContext* context,
                                      const UpdateBatteryStateRequest* request,
                                      BatteryState* reply) override
            ABSL_LOCKS_EXCLUDED(update_mutex_);

    static void PopulateBatteryProto(const Battery& in, BatteryState* out);

  private:
    void PopulateBatteryState(BatteryState* state) const ABSL_LOCKS_EXCLUDED(battery_mutex_);
    void OnBatteryUpdated(const Battery& state) ABSL_LOCKS_EXCLUDED(battery_mutex_);

    ObservableBattery& observable_battery_;
    const Options options_;
    absl::Mutex update_mutex_;

    mutable absl::Mutex battery_mutex_;
    BatteryState current_battery_ ABSL_GUARDED_BY(battery_mutex_);
    BatteryEventSource battery_events_;
    ObservableBattery::CallbackId battery_callback_id_{ObservableBattery::kInvalidCallbackId};
};

}  // namespace goldfish::grpc::v2
