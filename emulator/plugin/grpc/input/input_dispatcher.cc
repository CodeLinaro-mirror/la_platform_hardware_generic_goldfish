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
#include "input_dispatcher.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "android/status/status_macros.h"
#include "touch_encoding.h"

namespace goldfish::grpc::v2 {

namespace {

using ::android::base::ClockType;
using ::android::emulation::control::EvDevEvents;
using ::android::emulation::v2::InputActivity;
using ::android::emulation::v2::InputEvent;
using ::android::emulation::v2::KeyAction;
using ::android::emulation::v2::KeyEvent;
using ::android::emulation::v2::PointerAction;
using ::android::emulation::v2::PointerEvent;

// The events an operation writes to one device.
struct Report {
    TouchDeviceId device;
    TouchDeviceSink* sink;
    EvDevEvents events;
};
using Reports = std::vector<Report>;

bool CreatesContact(PointerAction action) {
    return action == PointerAction::POINTER_ACTION_DOWN ||
           action == PointerAction::POINTER_ACTION_HOVER_ENTER ||
           action == PointerAction::POINTER_ACTION_HOVER_MOVE;
}

EvDevEvents& EventsFor(Reports& reports, TouchDeviceId device) {
    auto it = std::ranges::find(reports, device, &Report::device);
    CHECK(it != reports.end()) << "Touch device " << device << " is not a target";
    return it->events;
}

// Makes `device` a target of the operation. Fails if it can't take a report.
absl::Status AddTarget(TouchDevices& devices, TouchDeviceId device, Reports& reports) {
    if (std::ranges::find(reports, device, &Report::device) != reports.end()) {
        return absl::OkStatus();
    }
    TouchDeviceSink* sink = devices.Find(device);
    if (sink == nullptr || !sink->IsActive()) {
        return absl::UnavailableError(
                absl::StrCat("Touch device ", device, " is not active; the event was dropped."));
    }
    reports.push_back({device, sink, {}});
    return absl::OkStatus();
}

void WriteReports(const ::goldfish::async::EventLoop& loop, const Reports& reports) {
    DCHECK(loop.IsOnLoopThread());
    for (const Report& report : reports) {
        if (!report.events.empty()) {
            report.sink->WriteReport(report.events);
        }
    }
}

void Complete(InputDispatcher::DispatchCallback& done, absl::Status status) {
    if (done) {
        std::move(done)(std::move(status));
    }
}

// A CANCEL from `source_id` on `display_id`, without pointers yet.
InputEvent CancelEvent(int32_t source_id, int32_t display_id) {
    InputEvent event;
    event.set_input_source_id(source_id);
    event.mutable_pointer()->set_display_id(display_id);
    event.mutable_pointer()->set_action(PointerAction::POINTER_ACTION_CANCEL);
    return event;
}

}  // namespace

InputDispatcher::InputDispatcher(::goldfish::async::EventLoop& loop, TouchDevices& devices,
                                 KeyboardSink& keyboard, KeyCodeResolver resolve_key_code,
                                 ::android::base::IClock* clock)
        : loop_(loop)
        , devices_(devices)
        , keyboard_(keyboard)
        , resolve_key_code_(std::move(resolve_key_code))
        , clock_(clock)
        , delivery_thread_([this] { DeliverActivity(); }) {}

InputDispatcher::~InputDispatcher() {
    {
        absl::MutexLock outbox_lock(outbox_mutex_);
        stopping_ = true;
    }
    delivery_thread_.join();
}

InputSessionId InputDispatcher::OpenSession(SessionOptions options) {
    const InputSessionId session = next_session_.fetch_add(1, std::memory_order_relaxed);
    // If the loop is gone, later operations of this session fail to post too.
    loop_.Post([this, session, options] { sessions_.emplace(session, options); }).IgnoreError();
    return session;
}

void InputDispatcher::CloseSession(InputSessionId session) {
    loop_.Post([this, session] { ReleaseSession(session); }).IgnoreError();
}

void InputDispatcher::DispatchPointer(InputSessionId session, PointerEvent event,
                                      DispatchCallback done) {
    PostOperation(
            [this, session, event = std::move(event)] { return ApplyPointer(session, event); },
            std::move(done));
}

void InputDispatcher::DispatchKey(InputSessionId session, KeyEvent event, DispatchCallback done) {
    PostOperation([this, session, event = std::move(event)] { return ApplyKey(session, event); },
                  std::move(done));
}

void InputDispatcher::DispatchText(InputSessionId session, std::string utf8,
                                   DispatchCallback done) {
    PostOperation([this, session, utf8 = std::move(utf8)] { return ApplyText(session, utf8); },
                  std::move(done));
}

void InputDispatcher::PostOperation(absl::AnyInvocable<absl::Status() &&> apply,
                                    DispatchCallback done) {
    // Shared so that `done` survives a rejected Post, which destroys the task.
    auto completion = std::make_shared<DispatchCallback>(std::move(done));
    absl::Status posted = loop_.Post([apply = std::move(apply), completion]() mutable {
        Complete(*completion, std::move(apply)());
    });
    if (!posted.ok()) {
        Complete(*completion, std::move(posted));
    }
}

absl::StatusOr<InputDispatcher::SessionOptions> InputDispatcher::FindSession(
        InputSessionId session) const {
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
        return absl::FailedPreconditionError(
                absl::StrCat("Input session ", session, " is not open."));
    }
    return it->second;
}

absl::Status InputDispatcher::ApplyPointer(InputSessionId session, const PointerEvent& event) {
    ASSIGN_OR_RETURN(SessionOptions options, FindSession(session));
    const PointerAction action = event.action();
    if (action == PointerAction::POINTER_ACTION_RELATIVE_MOVE ||
        action == PointerAction::POINTER_ACTION_SCROLL) {
        return absl::OkStatus();
    }
    if (event.pointers().empty() && (action == PointerAction::POINTER_ACTION_CANCEL ||
                                     action == PointerAction::POINTER_ACTION_HOVER_EXIT)) {
        return CancelOnDisplay(session, options.source_id, event.display_id());
    }

    // Find every target device before changing anything, so a rejected event
    // leaves no trace. Pointers of one event share the action, so an event
    // either creates contacts or releases them, never both.
    Reports reports;
    std::optional<TouchDeviceId> resolved;
    for (const PointerEvent::Pointer& pointer : event.pointers()) {
        if (std::optional<Contact> held = contacts_.Find({session, pointer.pointer_id()})) {
            RETURN_IF_ERROR(AddTarget(devices_, held->device, reports));
        } else if (CreatesContact(action)) {
            if (!resolved) {
                ASSIGN_OR_RETURN(resolved, devices_.Resolve(event.display_id()));
            }
            RETURN_IF_ERROR(AddTarget(devices_, *resolved, reports));
        }
    }

    // The event as reported: only the pointers written to a device.
    InputEvent applied;
    applied.set_input_source_id(options.source_id);
    PointerEvent& reported = *applied.mutable_pointer();
    reported = event;
    reported.clear_pointers();
    absl::Status result;
    for (const PointerEvent::Pointer& pointer : event.pointers()) {
        const ContactKey key{session, pointer.pointer_id()};
        if (std::optional<Contact> held = contacts_.Find(key)) {
            EvDevEvents& events = EventsFor(reports, held->device);
            if (IsContactReleased(action, pointer)) {
                contacts_.Release(key);
                AppendContactUp(held->slot, events);
            } else {
                AppendContactDownOrMove(pointer, action, held->slot, /*new_contact=*/false, events);
            }
            *reported.add_pointers() = pointer;
        } else if (CreatesContact(action)) {
            absl::StatusOr<int> slot =
                    contacts_.Acquire(key, *resolved, event.display_id(), options.human);
            if (!slot.ok()) {
                result.Update(slot.status());
                continue;
            }
            AppendContactDownOrMove(pointer, action, *slot, /*new_contact=*/true,
                                    EventsFor(reports, *resolved));
            *reported.add_pointers() = pointer;
        }
    }
    WriteReports(loop_, reports);
    if (!reported.pointers().empty()) {
        Report(std::move(applied));
    }
    return result;
}

absl::Status InputDispatcher::CancelOnDisplay(InputSessionId session, int32_t source_id,
                                              int32_t display_id) {
    ASSIGN_OR_RETURN(TouchDeviceId device, devices_.Resolve(display_id));
    Reports reports;
    RETURN_IF_ERROR(AddTarget(devices_, device, reports));
    InputEvent cancel = CancelEvent(source_id, display_id);
    for (const auto& [key, contact] : contacts_.ReleaseSessionOn(session, device)) {
        AppendContactUp(contact.slot, reports.front().events);
        cancel.mutable_pointer()->add_pointers()->set_pointer_id(key.pointer_id);
    }
    WriteReports(loop_, reports);
    if (!cancel.pointer().pointers().empty()) {
        Report(std::move(cancel));
    }
    return absl::OkStatus();
}

absl::Status InputDispatcher::ApplyKey(InputSessionId session, const KeyEvent& event) {
    ASSIGN_OR_RETURN(SessionOptions options, FindSession(session));
    const uint32_t code = resolve_key_code_(event);
    if (code == 0) {
        return absl::InvalidArgumentError("The key has no evdev code; the event was dropped.");
    }
    switch (event.action()) {
    case KeyAction::KEY_ACTION_DOWN:
        HoldKey(session, code);
        break;
    case KeyAction::KEY_ACTION_UP:
        if (!ReleaseKey(session, code)) {
            return absl::OkStatus();
        }
        break;
    default:
        HoldKey(session, code);
        ReleaseKey(session, code);
        break;
    }
    InputEvent applied;
    applied.set_input_source_id(options.source_id);
    *applied.mutable_key() = event;
    Report(std::move(applied));
    return absl::OkStatus();
}

void InputDispatcher::HoldKey(InputSessionId session, uint32_t code) {
    key_holders_[code].insert(session);
    keyboard_.WriteKey(code, /*down=*/true);
}

bool InputDispatcher::ReleaseKey(InputSessionId session, uint32_t code) {
    auto holders = key_holders_.find(code);
    if (holders == key_holders_.end() || holders->second.erase(session) == 0) {
        return false;
    }
    if (holders->second.empty()) {
        key_holders_.erase(holders);
        keyboard_.WriteKey(code, /*down=*/false);
    }
    return true;
}

absl::Status InputDispatcher::ApplyText(InputSessionId session, std::string_view utf8) {
    ASSIGN_OR_RETURN(SessionOptions options, FindSession(session));
    if (utf8.empty()) {
        return absl::OkStatus();
    }
    keyboard_.TypeText(utf8);
    InputEvent typed;
    typed.set_input_source_id(options.source_id);
    typed.mutable_text()->set_text(std::string(utf8));
    Report(std::move(typed));
    return absl::OkStatus();
}

void InputDispatcher::ReleaseSession(InputSessionId session) {
    auto options = sessions_.find(session);
    if (options == sessions_.end()) {
        return;  // Only open sessions hold contacts or keys.
    }
    const int32_t source_id = options->second.source_id;
    sessions_.erase(options);
    // Contacts on inactive devices are forgotten without a report.
    Reports reports;
    // One CANCEL per display the contacts were requested on, as activity
    // reports the requested display, not the device it resolved to.
    std::vector<InputEvent> cancels;
    for (const auto& [key, contact] : contacts_.ReleaseSession(session)) {
        if (!AddTarget(devices_, contact.device, reports).ok()) {
            continue;
        }
        AppendContactUp(contact.slot, EventsFor(reports, contact.device));
        auto cancel = std::ranges::find(cancels, contact.display_id, [](const InputEvent& event) {
            return event.pointer().display_id();
        });
        if (cancel == cancels.end()) {
            cancel = cancels.insert(cancel, CancelEvent(source_id, contact.display_id));
        }
        cancel->mutable_pointer()->add_pointers()->set_pointer_id(key.pointer_id);
    }
    WriteReports(loop_, reports);
    for (InputEvent& cancel : cancels) {
        Report(std::move(cancel), /*synthesized=*/true);
    }

    std::vector<uint32_t> held;
    for (const auto& [code, holders] : key_holders_) {
        if (holders.contains(session)) {
            held.push_back(code);
        }
    }
    std::ranges::sort(held);
    for (uint32_t code : held) {
        ReleaseKey(session, code);
        InputEvent up;
        up.set_input_source_id(source_id);
        up.mutable_key()->set_action(KeyAction::KEY_ACTION_UP);
        up.mutable_key()->set_evdev_code(static_cast<int32_t>(code));
        Report(std::move(up), /*synthesized=*/true);
    }
}

void InputDispatcher::Report(InputEvent event, bool synthesized) {
    DCHECK(loop_.IsOnLoopThread());
    InputActivity activity;
    *activity.mutable_event() = std::move(event);
    activity.set_dispatched_at_nanos(absl::ToUnixNanos(clock_->Now(ClockType::kRealtime)));
    activity.set_synthesized(synthesized);
    absl::MutexLock outbox_lock(outbox_mutex_);
    outbox_.push_back({next_sequence_++, std::move(activity)});
}

void InputDispatcher::DeliverActivity() {
    uint64_t next_delivery = 0;
    while (true) {
        std::deque<SequencedActivity> batch;
        {
            absl::MutexLock outbox_lock(outbox_mutex_);
            outbox_mutex_.Await(absl::Condition(this, &InputDispatcher::HasActivityOrStopping));
            if (outbox_.empty()) {
                return;  // Stopping, and everything queued was delivered.
            }
            batch.swap(outbox_);
        }
        for (const SequencedActivity& queued : batch) {
            DCHECK_EQ(queued.sequence, next_delivery);
            next_delivery = queued.sequence + 1;
            activity_events_.FireEvent(queued.activity);
        }
    }
}

bool InputDispatcher::HasActivityOrStopping() const {
    return !outbox_.empty() || stopping_;
}

}  // namespace goldfish::grpc::v2
