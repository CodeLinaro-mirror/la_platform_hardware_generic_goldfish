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
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"

#include "android/status/status_macros.h"
#include "touch_encoding.h"

namespace goldfish::grpc::v2 {

namespace {

using ::android::emulation::control::EvDevEvents;
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

}  // namespace

InputDispatcher::InputDispatcher(::goldfish::async::EventLoop& loop, TouchDevices& devices)
        : loop_(loop), devices_(devices) {}

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
    // Shared so that `done` survives a rejected Post, which destroys the task.
    auto completion = std::make_shared<DispatchCallback>(std::move(done));
    absl::Status posted = loop_.Post([this, session, event = std::move(event), completion] {
        Complete(*completion, ApplyPointer(session, event));
    });
    if (!posted.ok()) {
        Complete(*completion, std::move(posted));
    }
}

absl::Status InputDispatcher::ApplyPointer(InputSessionId session, const PointerEvent& event) {
    auto options = sessions_.find(session);
    if (options == sessions_.end()) {
        return absl::FailedPreconditionError(
                absl::StrCat("Input session ", session, " is not open."));
    }
    const PointerAction action = event.action();
    if (action == PointerAction::POINTER_ACTION_RELATIVE_MOVE ||
        action == PointerAction::POINTER_ACTION_SCROLL) {
        return absl::OkStatus();
    }
    if (event.pointers().empty() && (action == PointerAction::POINTER_ACTION_CANCEL ||
                                     action == PointerAction::POINTER_ACTION_HOVER_EXIT)) {
        return CancelOnDisplay(session, event.display_id());
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
        } else if (CreatesContact(action)) {
            absl::StatusOr<int> slot = contacts_.Acquire(key, *resolved, options->second.human);
            if (!slot.ok()) {
                result.Update(slot.status());
                continue;
            }
            AppendContactDownOrMove(pointer, action, *slot, /*new_contact=*/true,
                                    EventsFor(reports, *resolved));
        }
    }
    WriteReports(loop_, reports);
    return result;
}

absl::Status InputDispatcher::CancelOnDisplay(InputSessionId session, int32_t display_id) {
    ASSIGN_OR_RETURN(TouchDeviceId device, devices_.Resolve(display_id));
    Reports reports;
    RETURN_IF_ERROR(AddTarget(devices_, device, reports));
    for (const auto& [key, contact] : contacts_.ReleaseSessionOn(session, device)) {
        AppendContactUp(contact.slot, reports.front().events);
    }
    WriteReports(loop_, reports);
    return absl::OkStatus();
}

void InputDispatcher::ReleaseSession(InputSessionId session) {
    sessions_.erase(session);
    // Contacts on inactive devices are forgotten without a report.
    Reports reports;
    for (const auto& [key, contact] : contacts_.ReleaseSession(session)) {
        if (AddTarget(devices_, contact.device, reports).ok()) {
            AppendContactUp(contact.slot, EventsFor(reports, contact.device));
        }
    }
    WriteReports(loop_, reports);
}

}  // namespace goldfish::grpc::v2
