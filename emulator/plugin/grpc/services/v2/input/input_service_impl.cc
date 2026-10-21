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

#include "input/input_service_impl.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/simple_async_grpc.h"
#include "emulator/plugin/grpc/input/touch_encoding.h"

namespace goldfish::grpc::v2 {

namespace {

using ::android::emulation::control::AbslStatusToGrpcStatus;
using ::android::emulation::v2::AxisType;
using ::android::emulation::v2::KeyAction;
using ::android::emulation::v2::KeyEvent;
using ::android::emulation::v2::PointerAction;
using ::android::emulation::v2::PointerEvent;

constexpr char kClientIdMetadataKey[] = "x-client-id";
// Reserves [0, kReservedPrimaryPointerIds) for the primary input source (source_id == 0) so
// hashed pointer IDs for secondary sources never collide with primary pointer IDs.
constexpr int32_t kReservedPrimaryPointerIds = 64;

int32_t ScopedPointerId(int32_t source_id, int32_t pointer_id) {
    if (source_id == 0) {
        return pointer_id;
    }
    const size_t combined = absl::HashOf(source_id, pointer_id);
    const int32_t positive = static_cast<int32_t>(combined & 0x7fffffffU);
    return positive < kReservedPrimaryPointerIds ? positive + kReservedPrimaryPointerIds : positive;
}

bool SameKeyIdentifier(const KeyEvent& a, const KeyEvent& b) {
    if (a.key_identifier_case() != b.key_identifier_case()) {
        return false;
    }
    switch (a.key_identifier_case()) {
    case KeyEvent::kDomCode:
        return a.dom_code() == b.dom_code();
    case KeyEvent::kAndroidKeycode:
        return a.android_keycode() == b.android_keycode();
    case KeyEvent::kEvdevCode:
        return a.evdev_code() == b.evdev_code();
    case KeyEvent::KEY_IDENTIFIER_NOT_SET:
        return true;
    }
    return false;
}

// Keeps `held_keys` equal to the keys that remain pressed in the guest after `key`.
void TrackHeldKey(const KeyEvent& key, std::vector<KeyEvent>& held_keys) {
    std::erase_if(held_keys, [&key](const KeyEvent& held) { return SameKeyIdentifier(held, key); });
    if (key.action() == KeyAction::KEY_ACTION_DOWN) {
        held_keys.push_back(key);
    }
}

// Returns the source ID derived from the caller's `x-client-id` header, or 0 (the human
// operator) when the header is absent.
int32_t ClientSourceId(const ::grpc::ServerContextBase& context) {
    const auto& metadata = context.client_metadata();
    const auto it = metadata.find(kClientIdMetadataKey);
    if (it == metadata.end() || it->second.empty()) {
        return 0;
    }
    // Never log raw metadata header values; hash to a numeric source_id only.
    return InputServiceImpl::DeriveSourceIdFromClientId(
            absl::string_view(it->second.data(), it->second.length()));
}

absl::Status WithContext(const absl::Status& status, absl::string_view context) {
    return {status.code(), absl::StrCat(context, ": ", status.message())};
}

::grpc::Status ReadOnlyError(absl::string_view rpc) {
    return {::grpc::StatusCode::PERMISSION_DENIED,
            absl::StrFormat("Cannot execute %s: InputService is running in read-only mode. "
                            "Connect to a read-write endpoint or enable input injection "
                            "permissions.",
                            rpc)};
}

absl::Status ValidateKeyIdentifier(const KeyEvent& key) {
    switch (key.key_identifier_case()) {
    case KeyEvent::kDomCode:
        if (key.dom_code().empty()) {
            return absl::InvalidArgumentError(
                    "Invalid KeyEvent.dom_code '': must be a non-empty W3C UI Events "
                    "KeyboardEvent.code string (e.g., 'KeyA', 'Enter', 'ArrowUp').");
        }
        return absl::OkStatus();
    case KeyEvent::kAndroidKeycode:
        if (InputSession::AndroidKeycodeToEvdev(key.android_keycode()) == 0) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Invalid KeyEvent.android_keycode '%d': must be a supported Android "
                    "system keycode (3=HOME, 4=BACK, 24=VOLUME_UP, 25=VOLUME_DOWN, 26=POWER, "
                    "27=CAMERA, 82=MENU, 84=SEARCH, 187=APP_SWITCH). Use KeyEvent.dom_code "
                    "(e.g., 'KeyA') or KeyEvent.evdev_code for general keyboard keys.",
                    key.android_keycode()));
        }
        return absl::OkStatus();
    case KeyEvent::kEvdevCode:
        if (key.evdev_code() <= 0) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Invalid KeyEvent.evdev_code '%d': must be a positive Linux evdev KEY_* "
                    "code (e.g., 30 for KEY_A, 116 for KEY_POWER).",
                    key.evdev_code()));
        }
        return absl::OkStatus();
    case KeyEvent::KEY_IDENTIFIER_NOT_SET:
        break;
    }
    return absl::InvalidArgumentError(
            "Missing KeyEvent.key_identifier: set one of 'dom_code' (e.g., 'KeyA'), "
            "'android_keycode' (e.g., 3 for HOME), or 'evdev_code' (e.g., 30 for KEY_A).");
}

}  // namespace

InputServiceImpl::InputServiceImpl(IMultiDisplay& multi_display,
                                   std::shared_ptr<IKeyEventSender> key_event_sender,
                                   Options options)
        : multi_display_(multi_display)
        , options_(std::move(options))
        , input_session_(multi_display_, std::move(key_event_sender), options_.hw_sensor_hinge) {}

int32_t InputServiceImpl::DeriveSourceIdFromClientId(absl::string_view client_id) {
    const size_t hash = absl::HashOf(client_id);
    const int32_t source_id = static_cast<int32_t>(hash & 0x7fffffffU);
    return source_id != 0 ? source_id : 1;
}

absl::Status InputServiceImpl::ValidateInputEvent(const InputEvent& event) const {
    if (event.timestamp_nanos() < 0) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Invalid InputEvent.timestamp_nanos '%d': must be >= 0 (or omit timestamp_nanos "
                "for immediate dispatch).",
                event.timestamp_nanos()));
    }

    switch (event.event_case()) {
    case InputEvent::kPointer:
        return ValidatePointerEvent(event.pointer());
    case InputEvent::kKey:
        if (!options_.hw_keyboard) {
            return absl::FailedPreconditionError(
                    "Cannot dispatch KeyEvent: hardware keyboard support (hw.keyboard) is disabled "
                    "on this device. Enable hw.keyboard in the device hardware configuration or "
                    "check that CapabilitiesService reports INPUT_FEATURE_KEYBOARD.");
        }
        return ValidateKeyIdentifier(event.key());
    case InputEvent::kAxis: {
        const auto& axis = event.axis();
        if (axis.axis() == AxisType::AXIS_TYPE_UNSPECIFIED) {
            return absl::InvalidArgumentError(
                    "Invalid AxisEvent.axis 'AXIS_TYPE_UNSPECIFIED': must specify a valid "
                    "AxisType such as 'AXIS_TYPE_ROTARY_ENCODER'.");
        }
        if (!std::isfinite(axis.value())) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Invalid AxisEvent.value '%g': must be a finite number.", axis.value()));
        }
        if (axis.axis() == AxisType::AXIS_TYPE_ROTARY_ENCODER && !options_.hw_rotary_input) {
            return absl::FailedPreconditionError(
                    "Cannot dispatch AxisEvent for 'AXIS_TYPE_ROTARY_ENCODER': rotary encoder "
                    "input (hw.rotaryInput) is disabled on this device. Enable hw.rotaryInput in "
                    "the device hardware configuration or check that CapabilitiesService reports "
                    "INPUT_FEATURE_ROTARY_ENCODER.");
        }
        return absl::OkStatus();
    }
    case InputEvent::kText:
        if (!options_.hw_keyboard) {
            return absl::FailedPreconditionError(
                    "Cannot dispatch TextEvent: keyboard/IME text input (hw.keyboard) is disabled "
                    "on this device. Enable hw.keyboard in the device hardware configuration or "
                    "check that CapabilitiesService reports INPUT_FEATURE_KEYBOARD.");
        }
        return absl::OkStatus();
    case InputEvent::EVENT_NOT_SET:
        break;
    }
    return absl::InvalidArgumentError(
            "Missing InputEvent.event payload: set one of 'pointer', 'key', 'axis', or 'text'.");
}

absl::Status InputServiceImpl::ValidatePointerEvent(const PointerEvent& pointer) const {
    if (pointer.display_id() < 0) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Invalid PointerEvent.display_id '%d': must be >= 0 (use '0' for the primary "
                "display).",
                pointer.display_id()));
    }
    if (static_cast<size_t>(pointer.display_id()) >= IMultiDisplay::kMaxDisplays) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Cannot dispatch PointerEvent to display_id '%d': exceeds maximum supported "
                "display_id '%d'. Specify '0' for the primary display, or query "
                "CapabilitiesService for active secondary display IDs.",
                pointer.display_id(), IMultiDisplay::kMaxDisplays - 1));
    }
    const auto display = multi_display_.GetActiveDisplay(
            static_cast<uint32_t>(pointer.display_id()), options_.hw_sensor_hinge);
    if (!display.ok()) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Cannot dispatch PointerEvent to display_id '%d': %s. Specify '0' for the primary "
                "display, or query CapabilitiesService for active secondary display IDs.",
                pointer.display_id(), display.status().message()));
    }

    // NaN and infinity have no evdev representation.
    if (!std::isfinite(pointer.scroll_x()) || !std::isfinite(pointer.scroll_y())) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Invalid PointerEvent scroll (scroll_x='%g', scroll_y='%g'): must be finite "
                "numbers.",
                pointer.scroll_x(), pointer.scroll_y()));
    }
    const int32_t max_touch_points = std::min(options_.max_touch_points, kDefaultMaxTouchPoints);
    for (int i = 0; i < pointer.pointers_size(); ++i) {
        const PointerEvent::Pointer& p = pointer.pointers(i);
        if (p.pointer_id() < 0 || p.pointer_id() >= max_touch_points) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Invalid Pointer.pointer_id '%d' at pointers[%d]: must be in range [0, %d].",
                    p.pointer_id(), i, max_touch_points - 1));
        }
        const std::pair<absl::string_view, float> fields[] = {
            {"x", p.x()},
            {"y", p.y()},
            {"pressure", p.pressure()},
            {"size", p.size()},
            {"orientation_radians", p.orientation_radians()},
            {"tilt_radians", p.tilt_radians()},
            {"delta_x", p.delta_x()},
            {"delta_y", p.delta_y()},
        };
        for (const auto& [field, value] : fields) {
            if (!std::isfinite(value)) {
                return absl::InvalidArgumentError(absl::StrFormat(
                        "Invalid Pointer.%s '%g' for pointer_id '%d' at pointers[%d]: must be a "
                        "finite number.",
                        field, value, p.pointer_id(), i));
            }
        }
    }
    return absl::OkStatus();
}

absl::Status InputServiceImpl::ValidateBatchRequest(const SendInputEventsRequest& request) const {
    std::optional<int64_t> first_ts;
    std::optional<int64_t> prev_ts;
    for (int i = 0; i < request.events_size(); ++i) {
        const InputEvent& event = request.events(i);
        if (absl::Status status = ValidateInputEvent(event); !status.ok()) {
            return WithContext(
                    status, absl::StrFormat("SendInputEvents failed validation at events[%d]", i));
        }
        if (!event.has_timestamp_nanos()) {
            continue;
        }
        const int64_t ts = event.timestamp_nanos();
        if (prev_ts.has_value() && ts < *prev_ts) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "SendInputEvents failed validation at events[%d]: timestamp_nanos '%d' is "
                    "less than previous timestamp_nanos '%d'. Event timestamps in a batch "
                    "must be monotonically non-decreasing.",
                    i, ts, *prev_ts));
        }
        first_ts = first_ts.value_or(ts);
        const absl::Duration span = absl::Nanoseconds(ts - *first_ts);
        if (span > options_.max_batch_duration) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "SendInputEvents batch duration '%s' (from timestamp_nanos '%d' to '%d') "
                    "exceeds maximum allowed batch duration '%s'. Split longer gestures across "
                    "multiple SendInputEvents requests or use StreamInputEvents.",
                    absl::FormatDuration(span), *first_ts, ts,
                    absl::FormatDuration(options_.max_batch_duration)));
        }
        prev_ts = ts;
    }
    return absl::OkStatus();
}

int32_t InputServiceImpl::ComputeAggregateButtonsLocked(int32_t display_id) const {
    if (display_id < 0 || static_cast<size_t>(display_id) >= IMultiDisplay::kMaxDisplays) {
        return 0;
    }
    int32_t aggregate = 0;
    for (const auto& [source_id, source_state] : source_states_) {
        aggregate |= source_state.displays[static_cast<size_t>(display_id)].button_mask;
    }
    return aggregate;
}

void InputServiceImpl::TrackPointerEventLocked(int32_t source_id, PointerEvent& pointer) {
    const PointerAction action = pointer.action();
    DisplayPointerState& display =
            source_states_[source_id].displays[static_cast<size_t>(pointer.display_id())];

    display.button_mask = 0;
    if (action != PointerAction::POINTER_ACTION_CANCEL) {
        for (const auto& p : pointer.pointers()) {
            display.button_mask |= p.button_mask();
        }
    }

    if (pointer.pointers().empty() && (action == PointerAction::POINTER_ACTION_CANCEL ||
                                       action == PointerAction::POINTER_ACTION_HOVER_EXIT)) {
        for (size_t id = 0; id < display.active_pointer_ids.size(); ++id) {
            if (display.active_pointer_ids.test(id)) {
                pointer.add_pointers()->set_pointer_id(static_cast<int32_t>(id));
            }
        }
        display.active_pointer_ids.reset();
    } else if (action != PointerAction::POINTER_ACTION_RELATIVE_MOVE &&
               action != PointerAction::POINTER_ACTION_SCROLL) {
        for (const auto& p : pointer.pointers()) {
            const auto id = static_cast<size_t>(p.pointer_id());
            if (IsContactReleased(action, p)) {
                display.active_pointer_ids.reset(id);
            } else {
                display.active_pointer_ids.set(id);
            }
        }
    }

    const int32_t aggregate_buttons = ComputeAggregateButtonsLocked(pointer.display_id());
    for (int i = 0; i < pointer.pointers_size(); ++i) {
        PointerEvent::Pointer* p = pointer.mutable_pointers(i);
        p->set_pointer_id(ScopedPointerId(source_id, p->pointer_id()));
        p->set_button_mask(i == 0 ? aggregate_buttons : 0);
    }
}

absl::StatusOr<int64_t> InputServiceImpl::DispatchSingleEvent(InputEvent event, int32_t source_id,
                                                              bool synthesized) {
    event.set_input_source_id(source_id);

    absl::MutexLock dispatch_lock(&dispatch_mutex_);
    InputEvent session_event = event;
    switch (event.event_case()) {
    case InputEvent::kPointer:
        TrackPointerEventLocked(source_id, *session_event.mutable_pointer());
        break;
    case InputEvent::kKey:
        TrackHeldKey(event.key(), source_states_[source_id].held_keys);
        break;
    case InputEvent::kAxis:
    case InputEvent::kText:
    case InputEvent::EVENT_NOT_SET:
        break;
    }

    if (absl::Status status = input_session_.DispatchInputEvent(session_event); !status.ok()) {
        return WithContext(status, absl::StrFormat("Failed to dispatch InputEvent for "
                                                   "input_source_id '%d'",
                                                   source_id));
    }
    const int64_t dispatched_at_nanos = absl::ToUnixNanos(options_.now_fn());

    InputActivity activity;
    *activity.mutable_event() = std::move(event);
    activity.set_dispatched_at_nanos(dispatched_at_nanos);
    activity.set_synthesized(synthesized);
    activity_events_.FireEvent(activity);

    return dispatched_at_nanos;
}

void InputServiceImpl::CleanupSources(const absl::flat_hash_set<int32_t>& source_ids) {
    std::vector<std::pair<int32_t, InputEvent>> releases;
    {
        absl::MutexLock dispatch_lock(&dispatch_mutex_);
        for (int32_t source_id : source_ids) {
            const auto it = source_states_.find(source_id);
            if (it == source_states_.end()) {
                continue;
            }
            for (size_t display_id = 0; display_id < it->second.displays.size(); ++display_id) {
                const DisplayPointerState& display = it->second.displays[display_id];
                if (display.active_pointer_ids.none() && display.button_mask == 0) {
                    continue;
                }
                InputEvent cancel;
                PointerEvent* pointer = cancel.mutable_pointer();
                pointer->set_display_id(static_cast<int32_t>(display_id));
                pointer->set_action(PointerAction::POINTER_ACTION_CANCEL);
                for (size_t id = 0; id < display.active_pointer_ids.size(); ++id) {
                    if (display.active_pointer_ids.test(id)) {
                        pointer->add_pointers()->set_pointer_id(static_cast<int32_t>(id));
                    }
                }
                if (pointer->pointers().empty()) {
                    // Without any contacts, a placeholder pointer carries the button release.
                    pointer->add_pointers();
                }
                releases.emplace_back(source_id, std::move(cancel));
            }
            for (const KeyEvent& held_key : it->second.held_keys) {
                InputEvent key_up;
                *key_up.mutable_key() = held_key;
                key_up.mutable_key()->set_action(KeyAction::KEY_ACTION_UP);
                releases.emplace_back(source_id, std::move(key_up));
            }
        }
    }

    for (auto& [source_id, release] : releases) {
        const absl::StatusOr<int64_t> dispatched =
                DispatchSingleEvent(std::move(release), source_id, /*synthesized=*/true);
        if (!dispatched.ok()) {
            LOG(WARNING) << "InputService::StreamInputEvents: failed to dispatch synthesized "
                            "disconnect cleanup event for input_source_id '"
                         << source_id << "': " << dispatched.status();
        }
    }
}

void InputServiceImpl::SleepUntil(absl::Time target) const {
    const absl::Time now = options_.now_fn();
    if (target > now) {
        options_.sleep_fn(target - now);
    }
}

::grpc::Status InputServiceImpl::SendInputEvents(::grpc::ServerContext* context,
                                                 const SendInputEventsRequest* request,
                                                 SendInputEventsResponse* response) {
    if (options_.read_only) {
        return ReadOnlyError("SendInputEvents");
    }
    if (absl::Status status = ValidateBatchRequest(*request); !status.ok()) {
        VLOG(1) << "InputService::SendInputEvents: " << status;
        return AbslStatusToGrpcStatus(status);
    }

    const int32_t client_source_id = ClientSourceId(*context);
    // Host time corresponding to client timestamp 0, anchored at the first timed event.
    std::optional<absl::Time> client_epoch;
    for (int i = 0; i < request->events_size(); ++i) {
        const InputEvent& event = request->events(i);
        if (event.has_timestamp_nanos()) {
            const absl::Duration client_time = absl::Nanoseconds(event.timestamp_nanos());
            if (!client_epoch.has_value()) {
                client_epoch = options_.now_fn() - client_time;
            }
            SleepUntil(*client_epoch + client_time);
        }
        if (context->IsCancelled()) {
            return ::grpc::Status::CANCELLED;
        }

        const int32_t source_id =
                event.input_source_id() != 0 ? event.input_source_id() : client_source_id;
        const absl::StatusOr<int64_t> dispatched_at =
                DispatchSingleEvent(event, source_id, /*synthesized=*/false);
        if (!dispatched_at.ok()) {
            return AbslStatusToGrpcStatus(
                    WithContext(dispatched_at.status(),
                                absl::StrFormat("SendInputEvents failed at events[%d]", i)));
        }
        if (i == 0) {
            response->set_first_event_dispatched_at_nanos(*dispatched_at);
        }
        response->set_last_event_dispatched_at_nanos(*dispatched_at);
        response->set_events_dispatched(i + 1);
    }
    return ::grpc::Status::OK;
}

// Immediately finishes a `ServerReadReactor` with a non-OK status (used when `StreamInputEvents`
// is rejected up front, e.g. in read-only mode).
class ErrorServerReader final : public ::grpc::ServerReadReactor<InputServiceImpl::InputEvent> {
  public:
    explicit ErrorServerReader(::grpc::Status status) { this->Finish(std::move(status)); }
    void OnDone() override { delete this; }
};

/**
 * @brief Asynchronous gRPC `ServerReadReactor` for `StreamInputEvents` with event-loop pacing.
 *
 * ### Read-Pacing State Machine
 * `::SimpleServerReader<InputEvent>` (`WithSimpleReader`) starts the first `StartRead()` upon
 * construction and automatically issues the next `StartRead()` whenever `Read()` returns `true`:
 * - **Immediate dispatch (`delay == 0`):** `Read()` validates and dispatches the event immediately
 *   and returns `true` so gRPC pulls the next message from the stream without delay.
 * - **Delayed dispatch (`delay > 0`):** Instead of sleeping on a gRPC callback thread, `Read()`
 *   schedules a one-shot timer on `options_.event_loop` (`ScheduleDelayed`) and returns `false`,
 *   pausing further gRPC reads. When the timer fires on the event-loop thread,
 *   `OnScheduledDispatch()` dispatches the event and explicitly calls `this->StartRead()` to
 *   resume reading the stream.
 *
 * Because `StartRead()` is withheld while a timer is pending, **at most one gRPC read OR one
 * scheduled timer is ever active at a time**. This preserves gRPC flow-control backpressure
 * without buffering an unbounded queue of future events in memory, and guarantees that
 * `prev_client_ts_`, `prev_target_time_`, and `dispatched_count_` are accessed sequentially
 * across reads and timer callbacks.
 *
 * ### Cancellation & Lock-Ordering Invariants
 * - `state_` (`kReading`, `kWaitingForTimer`, `kDispatchingTimer`, `kFinished`) and `cancelled_`
 *   ensure that at most one thread ever claims ownership of calling `FinishStream()`.
 * - If the client cancels in `kReading`, gRPC completes the in-flight read with `ok = false` and
 *   drives teardown via `OnReadDone(false)` / `OnDone()`.
 * - If the client cancels in `kWaitingForTimer`, no gRPC read is in flight, so `OnCancel()` cancels
 *   `pending_timer_` and calls `FinishStream(Status::CANCELLED)`. If `OnScheduledDispatch()` had
 *   already begun on the event-loop thread, it either sees `cancelled_ == true` and returns without
 *   finishing, or had already transitioned `state_` to `kDispatchingTimer` so `OnScheduledDispatch`
 *   itself finishes the stream after `DispatchSingleEvent` completes.
 * - `EventLoop::Timer::Cancel()` may block on the event-loop thread (`PostAndWait`), while
 *   `OnScheduledDispatch()` runs on the event-loop thread and acquires `this->reactor_lock_`. To
 *   prevent deadlock, `pending_timer_` is always moved into a local variable under
 *   `this->reactor_lock_` and `Cancel()` is called **outside** the lock.
 *
 * ### Per-Stream Source Cleanup
 * Every `input_source_id` touched by the stream is tracked in `stream_sources_`. `FinishStream()`
 * (and `OnDone()` as a fallback) drains `stream_sources_` and calls `service_.CleanupSources()`
 * *before* `ServerReadReactor::Finish()`, ensuring any depressed touch slots or held keys are
 * released before the client observes RPC completion.
 */
class InputServiceImpl::StreamInputReader final : public ::SimpleServerReader<InputEvent> {
  public:
    StreamInputReader(InputServiceImpl& service, ::grpc::CallbackServerContext* context,
                      StreamInputEventsResponse* response)
            : service_(service), client_source_id_(ClientSourceId(*context)), response_(response) {}

    bool Read(const InputEvent* event) override {
        const int32_t source_id =
                event->input_source_id() != 0 ? event->input_source_id() : client_source_id_;
        if (absl::Status status = service_.ValidateInputEvent(*event); !status.ok()) {
            int32_t failed_index = 0;
            {
                absl::MutexLock lock(&this->reactor_lock_);
                failed_index = dispatched_count_;
            }
            status = WithContext(status,
                                 absl::StrFormat("StreamInputEvents failed validation at event #%d "
                                                 "(input_source_id '%d')",
                                                 failed_index, source_id));
            VLOG(1) << "InputService::StreamInputEvents: " << status;
            FinishStream(AbslStatusToGrpcStatus(status));
            return false;
        }

        absl::Duration delay = absl::ZeroDuration();
        if (event->has_timestamp_nanos()) {
            // Preserve the client's inter-event spacing, but cap each step so that a pause
            // between bursts (or a timestamp going backwards) rebases onto the host clock
            // instead of delaying dispatch.
            const absl::Time now = service_.options_.now_fn();
            absl::Time target = now;
            if (prev_client_ts_.has_value() && event->timestamp_nanos() > *prev_client_ts_) {
                const absl::Duration step =
                        std::min(absl::Nanoseconds(event->timestamp_nanos() - *prev_client_ts_),
                                 service_.options_.max_stream_step_duration);
                target = std::max(target, prev_target_time_ + step);
            }
            prev_client_ts_ = event->timestamp_nanos();
            prev_target_time_ = target;
            if (target > now) {
                delay = target - now;
            }
        }

        if (delay > absl::ZeroDuration()) {
            if (service_.options_.event_loop != nullptr) {
                return ScheduleDelayedDispatch(*event, source_id, delay);
            }
            service_.options_.sleep_fn(delay);
        }

        return DispatchAndRecord(*event, source_id);
    }

    void OnReadDone(bool ok) override {
        if (!ok) {
            FinishStream(::grpc::Status::OK);
            return;
        }
        ::SimpleServerReader<InputEvent>::OnReadDone(true);
    }

    void OnCancel() override {
        std::shared_ptr<::goldfish::async::EventLoop::Timer> timer_to_cancel;
        bool should_finish = false;
        {
            absl::MutexLock lock(&this->reactor_lock_);
            cancelled_ = true;
            if (state_ == State::kWaitingForTimer) {
                timer_to_cancel = std::move(pending_timer_);
                should_finish = true;
            }
        }
        if (timer_to_cancel != nullptr) {
            timer_to_cancel->Cancel();
        }
        if (should_finish) {
            FinishStream(::grpc::Status::CANCELLED);
        }
    }

    void OnDone() override {
        std::shared_ptr<::goldfish::async::EventLoop::Timer> timer_to_cancel;
        absl::flat_hash_set<int32_t> sources_to_clean;
        {
            absl::MutexLock lock(&this->reactor_lock_);
            timer_to_cancel = std::move(pending_timer_);
            sources_to_clean = std::move(stream_sources_);
            stream_sources_.clear();
        }
        if (timer_to_cancel != nullptr) {
            timer_to_cancel->Cancel();
        }
        if (!sources_to_clean.empty()) {
            service_.CleanupSources(sources_to_clean);
        }
        delete this;
    }

  private:
    enum class State {
        kReading,
        kWaitingForTimer,
        kDispatchingTimer,
        kFinished,
    };

    void FinishStream(::grpc::Status status) {
        absl::flat_hash_set<int32_t> sources_to_clean;
        {
            absl::MutexLock lock(&this->reactor_lock_);
            if (state_ == State::kFinished) {
                return;
            }
            state_ = State::kFinished;
            response_->set_events_dispatched(dispatched_count_);
            sources_to_clean = std::move(stream_sources_);
            stream_sources_.clear();
        }
        if (!sources_to_clean.empty()) {
            service_.CleanupSources(sources_to_clean);
        }
        ::grpc::ServerReadReactor<InputEvent>::Finish(std::move(status));
    }

    bool DispatchAndRecord(InputEvent event, int32_t source_id) {
        bool should_finish_cancelled = false;
        {
            absl::MutexLock lock(&this->reactor_lock_);
            if (state_ == State::kFinished) {
                return false;
            }
            if (cancelled_) {
                should_finish_cancelled = true;
            } else {
                stream_sources_.insert(source_id);
            }
        }
        if (should_finish_cancelled) {
            FinishStream(::grpc::Status::CANCELLED);
            return false;
        }

        const absl::StatusOr<int64_t> dispatched =
                service_.DispatchSingleEvent(std::move(event), source_id, /*synthesized=*/false);
        int32_t failed_index = 0;
        {
            absl::MutexLock lock(&this->reactor_lock_);
            if (dispatched.ok()) {
                ++dispatched_count_;
                return true;
            }
            failed_index = dispatched_count_;
        }
        FinishStream(AbslStatusToGrpcStatus(WithContext(
                dispatched.status(),
                absl::StrFormat("StreamInputEvents failed at event #%d", failed_index))));
        return false;
    }

    bool ScheduleDelayedDispatch(InputEvent event, int32_t source_id, absl::Duration delay) {
        {
            absl::MutexLock lock(&this->reactor_lock_);
            if (state_ == State::kFinished) {
                return false;
            }
            if (!cancelled_) {
                state_ = State::kWaitingForTimer;
                pending_timer_ = service_.options_.event_loop->ScheduleDelayed(
                        [this, event = std::move(event), source_id]() mutable {
                            OnScheduledDispatch(std::move(event), source_id);
                        },
                        delay);
                return false;
            }
        }
        FinishStream(::grpc::Status::CANCELLED);
        return false;
    }

    void OnScheduledDispatch(InputEvent event, int32_t source_id) {
        {
            absl::MutexLock lock(&this->reactor_lock_);
            if (cancelled_ || state_ != State::kWaitingForTimer) {
                return;
            }
            state_ = State::kDispatchingTimer;
            pending_timer_.reset();
        }
        if (!DispatchAndRecord(std::move(event), source_id)) {
            return;
        }
        bool should_finish_cancelled = false;
        {
            absl::MutexLock lock(&this->reactor_lock_);
            if (state_ == State::kFinished) {
                return;
            }
            if (cancelled_) {
                should_finish_cancelled = true;
            } else {
                state_ = State::kReading;
            }
        }
        if (should_finish_cancelled) {
            FinishStream(::grpc::Status::CANCELLED);
            return;
        }
        this->StartRead();
    }

    InputServiceImpl& service_;
    const int32_t client_source_id_;
    StreamInputEventsResponse* const response_;

    std::optional<int64_t> prev_client_ts_;
    absl::Time prev_target_time_;

    int32_t dispatched_count_ ABSL_GUARDED_BY(this->reactor_lock_){0};
    absl::flat_hash_set<int32_t> stream_sources_ ABSL_GUARDED_BY(this->reactor_lock_);
    std::shared_ptr<::goldfish::async::EventLoop::Timer> pending_timer_
            ABSL_GUARDED_BY(this->reactor_lock_);
    State state_ ABSL_GUARDED_BY(this->reactor_lock_){State::kReading};
    bool cancelled_ ABSL_GUARDED_BY(this->reactor_lock_){false};
};

::grpc::ServerReadReactor<InputServiceImpl::InputEvent>* InputServiceImpl::StreamInputEvents(
        ::grpc::CallbackServerContext* context, StreamInputEventsResponse* response) {
    if (options_.read_only) {
        return new ErrorServerReader(ReadOnlyError("StreamInputEvents"));
    }
    return new StreamInputReader(*this, context, response);
}

::grpc::ServerWriteReactor<InputServiceImpl::InputActivity>* InputServiceImpl::StreamInputActivity(
        ::grpc::CallbackServerContext* /*context*/, const StreamInputActivityRequest* request) {
    if (!options_.supports_input_activity_observation) {
        return new ::ErrorServerWriter<InputActivity>(::grpc::Status(
                ::grpc::StatusCode::PERMISSION_DENIED,
                "Cannot subscribe to StreamInputActivity: input activity observation is disabled "
                "on this endpoint. Query CapabilitiesService to check "
                "InputCapabilities.supports_input_activity_observation, or enable activity "
                "observation in the server configuration."));
    }
    return CreateInputActivityWriter<>(*request);
}

}  // namespace goldfish::grpc::v2
