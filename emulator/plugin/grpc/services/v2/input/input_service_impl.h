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

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/emulation/control/grpc_event_stream_support.h"
#include "android/emulation/control/keyboard/key_event_sender.h"
#include "emulator/plugin/grpc/input/input_session.h"
#include "emulator/plugin/grpc/input/slot_registry.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/display/abstract_multi_display.h"
#include "goldfish/eventing/event_sources.h"
#include "input_service.grpc.pb.h"

namespace goldfish::grpc::v2 {

using InputServiceBase =
        ::android::emulation::v2::InputService::WithCallbackMethod_StreamInputEvents<
                ::android::emulation::v2::InputService::WithCallbackMethod_StreamInputActivity<
                        ::android::emulation::v2::InputService::Service>>;

/**
 * @brief Implements the AEMU v2 InputService.
 *
 * Injects touch, mouse, stylus, keyboard, rotary axis, and IME text events into
 * the guest OS, and broadcasts dispatched input activity to observers.
 *
 * ### Pacing & Timelines:
 * - `SendInputEvents` validates batch monotonicity and duration bounds up front,
 *   then paces events in real time against the host monotonic clock (`NowFn`)
 *   according to `timestamp_nanos` deltas.
 * - `StreamInputEvents` rebases client `timestamp_nanos` deltas onto the host
 *   clock, caps stale inter-burst gaps to prevent artificial lag, and schedules
 *   delayed dispatches asynchronously on the `EventLoop`.
 *
 * ### Per-Source Isolation & Disconnect Cleanup:
 * - Active pointer slots, mouse button masks, and depressed keys are tracked
 *   independently per resolved `input_source_id`.
 * - When `input_source_id` is `0` and the caller provides a non-empty
 *   `x-client-id` gRPC metadata header, a deterministic non-zero source ID is
 *   derived automatically.
 * - When a `StreamInputEvents` stream closes or is cancelled, any pointers or
 *   keys still held by that stream's sources are automatically released via
 *   synthesized `POINTER_ACTION_CANCEL` and `KEY_ACTION_UP` events, which are
 *   also reported on `StreamInputActivity` with `synthesized = true`.
 *
 * ### Thread Safety:
 * - Thread-safe across concurrent `SendInputEvents`, `StreamInputEvents`, and
 *   `StreamInputActivity` RPC invocations. Hardware dispatch, per-source state
 *   mutations, and activity notifications are serialized under `dispatch_mutex_`
 *   so observers receive events in exact dispatch order.
 */
class InputServiceImpl final : public InputServiceBase {
  public:
    static constexpr absl::Duration kDefaultMaxBatchDuration = absl::Seconds(30);
    static constexpr absl::Duration kDefaultMaxStreamStepDuration = absl::Seconds(1);
    static constexpr int32_t kDefaultMaxTouchPoints =
            static_cast<int32_t>(::android::emulation::control::kMtsPointersNum);

    using IMultiDisplay = ::goldfish::display::IMultiDisplay;
    using IKeyEventSender = ::android::emulation::control::keyboard::IKeyEventSender;

    using InputEvent = ::android::emulation::v2::InputEvent;
    using PointerEvent = ::android::emulation::v2::PointerEvent;
    using KeyEvent = ::android::emulation::v2::KeyEvent;
    using SendInputEventsRequest = ::android::emulation::v2::SendInputEventsRequest;
    using SendInputEventsResponse = ::android::emulation::v2::SendInputEventsResponse;
    using StreamInputEventsResponse = ::android::emulation::v2::StreamInputEventsResponse;
    using StreamInputActivityRequest = ::android::emulation::v2::StreamInputActivityRequest;
    using InputActivity = ::android::emulation::v2::InputActivity;

    using ActivityEventSource = ::android::base::eventing::CallbackEventSource<InputActivity>;
    using NowFn = std::function<absl::Time()>;
    using SleepFn = std::function<void(absl::Duration)>;

    template <typename Reactor = ::grpc::ServerWriteReactor<InputActivity>>
    using InputActivityWriter = ::android::emulation::control::UniqueEventStreamWriter<
            InputActivity, ActivityEventSource, 0, 0, Reactor>;

    struct Options {
        bool hw_sensor_hinge{false};
        bool hw_keyboard{true};
        bool hw_rotary_input{true};
        bool read_only{false};
        bool supports_input_activity_observation{true};
        int32_t max_touch_points{kDefaultMaxTouchPoints};
        absl::Duration max_batch_duration{kDefaultMaxBatchDuration};
        absl::Duration max_stream_step_duration{kDefaultMaxStreamStepDuration};
        ::goldfish::async::EventLoop* event_loop{nullptr};
        NowFn now_fn{[]() { return absl::Now(); }};
        SleepFn sleep_fn{[](absl::Duration d) { absl::SleepFor(d); }};
    };

    InputServiceImpl(IMultiDisplay& multi_display,
                     std::shared_ptr<IKeyEventSender> key_event_sender, Options options);

    ::grpc::Status SendInputEvents(::grpc::ServerContext* context,
                                   const SendInputEventsRequest* request,
                                   SendInputEventsResponse* response) override;

    ::grpc::ServerReadReactor<InputEvent>* StreamInputEvents(
            ::grpc::CallbackServerContext* context, StreamInputEventsResponse* response) override;

    /**
     * @brief Creates the activity reactor without the observation policy check, so tests can
     * substitute an in-memory `Reactor`.
     */
    template <typename Reactor = ::grpc::ServerWriteReactor<InputActivity>>
    auto* CreateInputActivityWriter(const StreamInputActivityRequest& request) {
        typename InputActivityWriter<Reactor>::FilterPredicate filter;
        if (request.has_input_source_id()) {
            const int32_t source_id = request.input_source_id();
            filter = [source_id](const InputActivity& activity) {
                return activity.event().input_source_id() == source_id;
            };
        }
        return new InputActivityWriter<Reactor>(&activity_events_, /*populate_fn=*/nullptr,
                                                std::move(filter));
    }

    ::grpc::ServerWriteReactor<InputActivity>* StreamInputActivity(
            ::grpc::CallbackServerContext* context,
            const StreamInputActivityRequest* request) override;

    /**
     * @brief Deterministically derives an input_source_id for a client identifier string.
     *
     * Used when `InputEvent.input_source_id` is 0 and the caller supplies a
     * non-empty `x-client-id` metadata header. Always returns a non-zero positive
     * 32-bit integer.
     */
    static int32_t DeriveSourceIdFromClientId(absl::string_view client_id);

  private:
    class StreamInputReader;

    struct DisplayPointerState {
        std::bitset<::android::emulation::control::kMtsPointersNum> active_pointer_ids;
        int32_t button_mask{0};
    };

    struct SourceState {
        std::array<DisplayPointerState, IMultiDisplay::kMaxDisplays> displays;
        std::vector<KeyEvent> held_keys;
    };

    absl::Status ValidateInputEvent(const InputEvent& event) const;
    absl::Status ValidatePointerEvent(const PointerEvent& pointer) const;
    absl::Status ValidateBatchRequest(const SendInputEventsRequest& request) const;
    absl::StatusOr<int64_t> DispatchSingleEvent(InputEvent event, int32_t source_id,
                                                bool synthesized)
            ABSL_LOCKS_EXCLUDED(dispatch_mutex_);

    /**
     * @brief Records the source's contacts and buttons from `pointer`, then rewrites it for the
     * shared `InputSession`.
     *
     * A pointer-less CANCEL or HOVER_EXIT is expanded to every contact the source holds on the
     * display, pointer IDs are mapped into the source's own slot namespace, and the first pointer
     * carries the union of all sources' buttons on the display.
     */
    void TrackPointerEventLocked(int32_t source_id, PointerEvent& pointer)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(dispatch_mutex_);
    void CleanupSources(const absl::flat_hash_set<int32_t>& source_ids)
            ABSL_LOCKS_EXCLUDED(dispatch_mutex_);
    int32_t ComputeAggregateButtonsLocked(int32_t display_id) const
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(dispatch_mutex_);
    void SleepUntil(absl::Time target) const;

    IMultiDisplay& multi_display_;
    const Options options_;

    absl::Mutex dispatch_mutex_;
    InputSession input_session_ ABSL_GUARDED_BY(dispatch_mutex_);
    absl::flat_hash_map<int32_t, SourceState> source_states_ ABSL_GUARDED_BY(dispatch_mutex_);

    ActivityEventSource activity_events_;
};

}  // namespace goldfish::grpc::v2
