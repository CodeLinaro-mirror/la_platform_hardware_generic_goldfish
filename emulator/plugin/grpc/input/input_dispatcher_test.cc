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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/types/span.h"

#include "goldfish/async/testing/test_event_loop.h"
#include "standard-headers/linux/input-event-codes.h"

namespace goldfish::grpc::v2 {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::android::emulation::control::EvDevEvent;
using ::android::emulation::v2::PointerAction;
using ::android::emulation::v2::PointerEvent;
using ::goldfish::async::EventLoop;
using ::goldfish::async::testing::TestEventLoop;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Optional;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

constexpr TouchDeviceId kInner = 0;
constexpr TouchDeviceId kOuter = 1;

// Converts a normalized coordinate to the evdev value the guest receives.
constexpr uint32_t Abs(float v) {
    return static_cast<uint32_t>(v * 32767.0F);
}

struct GuestContact {
    int slot;
    int32_t tracking_id;
    uint32_t x;
    uint32_t y;
};

auto ContactAt(float x, float y) {
    return ::testing::AllOf(Field(&GuestContact::x, Abs(x)), Field(&GuestContact::y, Abs(y)));
}

// Minimal model of the guest's Linux multi-touch protocol B state: ABS_MT_SLOT
// selects a slot, later ABS_MT_* values update it, and a slot holds a contact
// while its tracking ID is not -1. Each WriteReport() call is one frame.
class GuestTouchState {
  public:
    void ApplyReport(absl::Span<const EvDevEvent> events) {
        for (const EvDevEvent& event : events) {
            ASSERT_EQ(event.type, EV_ABS) << "the sink, not the dispatcher, appends SYN_REPORT";
            if (event.code == ABS_MT_SLOT) {
                ASSERT_LT(event.value, slots_.size());
                selected_ = static_cast<int>(event.value);
                continue;
            }
            Slot& slot = slots_[selected_];
            if (event.code == ABS_MT_TRACKING_ID) {
                slot.tracking_id = static_cast<int32_t>(event.value);
            } else if (event.code == ABS_MT_POSITION_X) {
                slot.x = event.value;
            } else if (event.code == ABS_MT_POSITION_Y) {
                slot.y = event.value;
            }
        }
    }

    std::vector<GuestContact> Active() const {
        std::vector<GuestContact> active;
        for (int i = 0; i < kTouchDeviceSlots; ++i) {
            if (slots_[i].tracking_id != -1) {
                active.push_back({i, slots_[i].tracking_id, slots_[i].x, slots_[i].y});
            }
        }
        return active;
    }

  private:
    struct Slot {
        int32_t tracking_id = -1;
        uint32_t x = 0;
        uint32_t y = 0;
    };
    int selected_ = 0;
    std::array<Slot, kTouchDeviceSlots> slots_{};
};

class FakeSink : public TouchDeviceSink {
  public:
    explicit FakeSink(const EventLoop& loop) : loop_(loop) {}

    bool IsActive() const override { return active; }
    void WriteReport(absl::Span<const EvDevEvent> events) override {
        reports.emplace_back(events.begin(), events.end());
        on_loop_thread.push_back(loop_.IsOnLoopThread());
        guest.ApplyReport(events);
        frames.push_back(guest.Active());
    }

    bool active = true;
    std::vector<std::vector<EvDevEvent>> reports;
    std::vector<bool> on_loop_thread;
    GuestTouchState guest;
    // The guest's active contacts after each report.
    std::vector<std::vector<GuestContact>> frames;

  private:
    const EventLoop& loop_;
};

class FakeTouchDevices : public TouchDevices {
  public:
    explicit FakeTouchDevices(const EventLoop& loop) : inner(loop), outer(loop) {}

    absl::StatusOr<TouchDeviceId> Resolve(int32_t display_id) override {
        if (!resolve_status.ok()) {
            return resolve_status;
        }
        auto it = remap.find(display_id);
        return it == remap.end() ? static_cast<TouchDeviceId>(display_id) : it->second;
    }

    TouchDeviceSink* Find(TouchDeviceId device) override {
        if (device == kInner) {
            return &inner;
        }
        return device == kOuter ? &outer : nullptr;
    }

    FakeSink inner;
    FakeSink outer;
    absl::flat_hash_map<int32_t, TouchDeviceId> remap;
    absl::Status resolve_status;
};

struct P {
    int32_t id;
    float x = 0.5F;
    float y = 0.5F;
    std::optional<float> pressure;
};

PointerEvent Touch(PointerAction action, std::vector<P> pointers, int32_t display_id = 0) {
    PointerEvent event;
    event.set_display_id(display_id);
    event.set_action(action);
    for (const P& p : pointers) {
        auto* pointer = event.add_pointers();
        pointer->set_pointer_id(p.id);
        pointer->set_x(p.x);
        pointer->set_y(p.y);
        if (p.pressure) {
            pointer->set_pressure(*p.pressure);
        }
    }
    return event;
}

constexpr PointerAction kDown = PointerAction::POINTER_ACTION_DOWN;
constexpr PointerAction kMove = PointerAction::POINTER_ACTION_MOVE;
constexpr PointerAction kUp = PointerAction::POINTER_ACTION_UP;

class InputDispatcherTest : public ::testing::Test {
  protected:
    // Dispatches `event` and runs the loop. @return The completion status.
    absl::Status Dispatch(InputSessionId session, PointerEvent event) {
        std::optional<absl::Status> result;
        dispatcher_.DispatchPointer(session, std::move(event),
                                    [&result](absl::Status status) { result = status; });
        loop_->RunAll();
        if (!result) {
            return absl::InternalError("done was not called");
        }
        return *result;
    }

    InputSessionId Agent() { return dispatcher_.OpenSession({}); }
    InputSessionId Human() { return dispatcher_.OpenSession({.human = true}); }

    std::unique_ptr<TestEventLoop> loop_ = TestEventLoop::Create();
    FakeTouchDevices devices_{*loop_};
    FakeSink& inner_ = devices_.inner;
    FakeSink& outer_ = devices_.outer;
    InputDispatcher dispatcher_{*loop_, devices_};
};

TEST_F(InputDispatcherTest, SessionIdsStartAtOneAndAreUnique) {
    EXPECT_EQ(Agent(), 1);
    EXPECT_EQ(Human(), 2);
    EXPECT_EQ(Agent(), 3);
}

TEST_F(InputDispatcherTest, SessionsSharingPointerIdGetDistinctContacts) {
    const InputSessionId a = Agent();
    const InputSessionId b = Agent();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.25F, 0.25F}})), IsOk());
    ASSERT_THAT(Dispatch(b, Touch(kDown, {{0, 0.75F, 0.75F}})), IsOk());

    const auto active = inner_.guest.Active();
    EXPECT_THAT(active, UnorderedElementsAre(ContactAt(0.25F, 0.25F), ContactAt(0.75F, 0.75F)));
    ASSERT_THAT(active, SizeIs(2));
    EXPECT_NE(active[0].slot, active[1].slot);
    EXPECT_NE(active[0].tracking_id, active[1].tracking_id);
}

TEST_F(InputDispatcherTest, UpReleasesOnlyThatSessionsContact) {
    const InputSessionId a = Agent();
    const InputSessionId b = Agent();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.25F, 0.25F}})), IsOk());
    ASSERT_THAT(Dispatch(b, Touch(kDown, {{0, 0.75F, 0.75F}})), IsOk());

    ASSERT_THAT(Dispatch(a, Touch(kUp, {{0, 0.25F, 0.25F}})), IsOk());

    EXPECT_THAT(inner_.guest.Active(), ElementsAre(ContactAt(0.75F, 0.75F)));
}

TEST_F(InputDispatcherTest, ContactStaysOnBoundDeviceWhenDisplayRemaps) {
    const InputSessionId a = Agent();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.25F, 0.25F}})), IsOk());

    devices_.remap[0] = kOuter;  // The foldable unfolds mid-gesture.
    ASSERT_THAT(Dispatch(a, Touch(kMove, {{0, 0.5F, 0.5F}})), IsOk());
    EXPECT_THAT(inner_.guest.Active(), ElementsAre(ContactAt(0.5F, 0.5F)));
    ASSERT_THAT(Dispatch(a, Touch(kUp, {{0, 0.5F, 0.5F}})), IsOk());

    EXPECT_THAT(inner_.reports, SizeIs(3));
    EXPECT_THAT(inner_.guest.Active(), IsEmpty());
    EXPECT_THAT(outer_.reports, IsEmpty());

    // A new contact goes to the newly resolved device.
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.75F, 0.75F}})), IsOk());
    EXPECT_THAT(outer_.guest.Active(), ElementsAre(ContactAt(0.75F, 0.75F)));
}

TEST_F(InputDispatcherTest, AgentsExhaustNonReservedSlotsButHumanStillGetsOne) {
    for (int i = 0; i < kTouchDeviceSlots - kReservedHumanSlots; ++i) {
        ASSERT_THAT(Dispatch(Agent(), Touch(kDown, {{0}})), IsOk());
    }
    const size_t reports = inner_.reports.size();

    EXPECT_THAT(Dispatch(Agent(), Touch(kDown, {{0}})),
                StatusIs(absl::StatusCode::kResourceExhausted));
    EXPECT_THAT(inner_.reports, SizeIs(reports));

    EXPECT_THAT(Dispatch(Human(), Touch(kDown, {{0}})), IsOk());
    EXPECT_THAT(inner_.guest.Active(), SizeIs(kTouchDeviceSlots - kReservedHumanSlots + 1));
}

TEST_F(InputDispatcherTest, ExhaustedPointerIsSkippedWhileOthersApply) {
    const InputSessionId a = Agent();
    std::vector<P> pointers;
    for (int i = 0; i <= kTouchDeviceSlots - kReservedHumanSlots; ++i) {
        pointers.push_back({i});
    }
    EXPECT_THAT(Dispatch(a, Touch(kDown, pointers)),
                StatusIs(absl::StatusCode::kResourceExhausted));
    EXPECT_THAT(inner_.reports, SizeIs(1));
    EXPECT_THAT(inner_.guest.Active(), SizeIs(kTouchDeviceSlots - kReservedHumanSlots));
}

TEST_F(InputDispatcherTest, InactiveDeviceRejectsEventAndKeepsState) {
    const InputSessionId a = Agent();
    inner_.active = false;
    EXPECT_THAT(Dispatch(a, Touch(kDown, {{0}})), StatusIs(absl::StatusCode::kUnavailable));
    EXPECT_THAT(inner_.reports, IsEmpty());

    inner_.active = true;
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.25F, 0.25F}})), IsOk());
    EXPECT_THAT(inner_.reports.back(), Contains(Field(&EvDevEvent::code, ABS_MT_TRACKING_ID)));

    // The contact survives an UP the device couldn't take.
    inner_.active = false;
    EXPECT_THAT(Dispatch(a, Touch(kUp, {{0}})), StatusIs(absl::StatusCode::kUnavailable));
    inner_.active = true;
    ASSERT_THAT(Dispatch(a, Touch(kMove, {{0, 0.5F, 0.5F}})), IsOk());
    EXPECT_THAT(inner_.reports, SizeIs(2));
    EXPECT_THAT(inner_.guest.Active(), ElementsAre(ContactAt(0.5F, 0.5F)));
}

TEST_F(InputDispatcherTest, MissingDeviceIsUnavailable) {
    EXPECT_THAT(Dispatch(Agent(), Touch(kDown, {{0}}, /*display_id=*/7)),
                StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(InputDispatcherTest, ResolveFailureChangesNothing) {
    const InputSessionId a = Agent();
    devices_.resolve_status = absl::NotFoundError("no display 3");
    EXPECT_THAT(Dispatch(a, Touch(kDown, {{0}}, 3)), StatusIs(absl::StatusCode::kNotFound));
    devices_.resolve_status = absl::OkStatus();
    EXPECT_THAT(Dispatch(a, Touch(kMove, {{0}})), IsOk());
    EXPECT_THAT(inner_.reports, IsEmpty());
}

TEST_F(InputDispatcherTest, MoveAndUpForUnheldPointerAreIgnored) {
    const InputSessionId a = Agent();
    EXPECT_THAT(Dispatch(a, Touch(kMove, {{0}})), IsOk());
    EXPECT_THAT(Dispatch(a, Touch(kUp, {{0}})), IsOk());
    EXPECT_THAT(inner_.reports, IsEmpty());
}

TEST_F(InputDispatcherTest, DownForHeldPointerMovesIt) {
    const InputSessionId a = Agent();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.25F, 0.25F}})), IsOk());
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.5F, 0.5F}})), IsOk());

    ASSERT_THAT(inner_.reports, SizeIs(2));
    EXPECT_THAT(inner_.reports[1], Not(Contains(Field(&EvDevEvent::code, ABS_MT_TRACKING_ID))));
    EXPECT_THAT(inner_.guest.Active(), ElementsAre(ContactAt(0.5F, 0.5F)));
}

TEST_F(InputDispatcherTest, RelativeMoveAndScrollChangeNothing) {
    const InputSessionId a = Agent();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.25F, 0.25F}})), IsOk());

    EXPECT_THAT(Dispatch(a, Touch(PointerAction::POINTER_ACTION_RELATIVE_MOVE, {{0, 0.5F, 0.5F}})),
                IsOk());
    EXPECT_THAT(Dispatch(a, Touch(PointerAction::POINTER_ACTION_SCROLL, {{0, 0.5F, 0.5F, 0.0F}})),
                IsOk());
    EXPECT_THAT(Dispatch(a, Touch(PointerAction::POINTER_ACTION_SCROLL, {{1}})), IsOk());

    EXPECT_THAT(inner_.reports, SizeIs(1));
    EXPECT_THAT(inner_.guest.Active(), ElementsAre(ContactAt(0.25F, 0.25F)));
}

TEST_F(InputDispatcherTest, HoverCreatesContactAndHoverExitReleasesIt) {
    const InputSessionId a = Agent();
    ASSERT_THAT(Dispatch(a, Touch(PointerAction::POINTER_ACTION_HOVER_ENTER, {{0}})), IsOk());
    EXPECT_THAT(inner_.guest.Active(), SizeIs(1));
    ASSERT_THAT(Dispatch(a, Touch(PointerAction::POINTER_ACTION_HOVER_EXIT, {{0}})), IsOk());
    EXPECT_THAT(inner_.guest.Active(), IsEmpty());
}

TEST_F(InputDispatcherTest, PointerlessCancelReleasesSessionContactsOnDisplay) {
    const InputSessionId a = Agent();
    const InputSessionId b = Agent();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0}, {1}})), IsOk());
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{2}}, 1)), IsOk());
    ASSERT_THAT(Dispatch(b, Touch(kDown, {{0, 0.75F, 0.75F}})), IsOk());

    ASSERT_THAT(Dispatch(a, Touch(PointerAction::POINTER_ACTION_CANCEL, {})), IsOk());

    EXPECT_THAT(inner_.reports, SizeIs(3));
    EXPECT_THAT(inner_.guest.Active(), ElementsAre(ContactAt(0.75F, 0.75F)));
    EXPECT_THAT(outer_.guest.Active(), SizeIs(1));
}

TEST_F(InputDispatcherTest, CloseSessionReleasesOnlyItsContactsOneReportPerDevice) {
    const InputSessionId a = Agent();
    const InputSessionId b = Human();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0}, {1}})), IsOk());
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{2}}, 1)), IsOk());
    ASSERT_THAT(Dispatch(b, Touch(kDown, {{0, 0.75F, 0.75F}})), IsOk());
    inner_.reports.clear();
    outer_.reports.clear();

    dispatcher_.CloseSession(a);
    loop_->RunAll();

    EXPECT_THAT(inner_.reports, SizeIs(1));
    EXPECT_THAT(outer_.reports, SizeIs(1));
    EXPECT_THAT(inner_.guest.Active(), ElementsAre(ContactAt(0.75F, 0.75F)));
    EXPECT_THAT(outer_.guest.Active(), IsEmpty());

    // The closed session's events no longer create contacts.
    EXPECT_THAT(Dispatch(a, Touch(kDown, {{0}})), StatusIs(absl::StatusCode::kFailedPrecondition));
    EXPECT_THAT(inner_.reports, SizeIs(1));
}

TEST_F(InputDispatcherTest, MultiPointerEventWritesOneReportPerDevice) {
    const InputSessionId a = Agent();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{0, 0.1F, 0.1F}, {1, 0.2F, 0.2F}, {2, 0.3F, 0.3F}})),
                IsOk());
    EXPECT_THAT(inner_.reports, SizeIs(1));
    EXPECT_THAT(inner_.guest.Active(), SizeIs(3));

    // A partial lift: pointer 1 keeps pressure, the others lift.
    ASSERT_THAT(Dispatch(a, Touch(kUp, {{0}, {1, 0.4F, 0.4F, 1.0F}, {2}})), IsOk());
    EXPECT_THAT(inner_.reports, SizeIs(2));
    EXPECT_THAT(inner_.guest.Active(), ElementsAre(ContactAt(0.4F, 0.4F)));
}

TEST_F(InputDispatcherTest, BackToBackEventsApplyInOrder) {
    const InputSessionId a = Agent();
    std::vector<int> completed;
    const std::vector<PointerEvent> events = {
        Touch(kDown, {{0, 0.1F, 0.1F}}), Touch(kMove, {{0, 0.2F, 0.2F}}),
        Touch(kMove, {{0, 0.3F, 0.3F}}), Touch(kUp, {{0, 0.3F, 0.3F}})};
    for (int i = 0; i < static_cast<int>(events.size()); ++i) {
        dispatcher_.DispatchPointer(a, events[i], [&completed, i](absl::Status status) {
            EXPECT_THAT(status, IsOk());
            completed.push_back(i);
        });
    }
    loop_->RunAll();

    EXPECT_THAT(completed, ElementsAre(0, 1, 2, 3));
    ASSERT_THAT(inner_.frames, SizeIs(4));
    EXPECT_THAT(inner_.frames[0], ElementsAre(ContactAt(0.1F, 0.1F)));
    EXPECT_THAT(inner_.frames[1], ElementsAre(ContactAt(0.2F, 0.2F)));
    EXPECT_THAT(inner_.frames[2], ElementsAre(ContactAt(0.3F, 0.3F)));
    EXPECT_THAT(inner_.frames[3], IsEmpty());
}

TEST_F(InputDispatcherTest, ReportsAndCompletionsRunOnLoopThread) {
    const InputSessionId a = Agent();
    std::optional<bool> done_on_loop;
    dispatcher_.DispatchPointer(a, Touch(kDown, {{0}, {1}}),
                                [&](absl::Status) { done_on_loop = loop_->IsOnLoopThread(); });
    loop_->RunAll();
    ASSERT_THAT(Dispatch(a, Touch(kDown, {{2}}, 1)), IsOk());
    dispatcher_.CloseSession(a);
    loop_->RunAll();

    EXPECT_EQ(done_on_loop, true);
    EXPECT_THAT(inner_.on_loop_thread, ElementsAre(true, true));
    EXPECT_THAT(outer_.on_loop_thread, ElementsAre(true, true));
}

TEST_F(InputDispatcherTest, RejectedPostCompletesOnCallingThread) {
    const InputSessionId a = Agent();
    loop_->RunAll();
    ASSERT_THAT(loop_->ShutdownAndWait(), IsOk());

    std::optional<absl::Status> result;
    std::thread::id done_thread;
    dispatcher_.DispatchPointer(a, Touch(kDown, {{0}}), [&](absl::Status status) {
        result = status;
        done_thread = std::this_thread::get_id();
    });

    EXPECT_THAT(result, Optional(StatusIs(absl::StatusCode::kUnavailable)));
    EXPECT_EQ(done_thread, std::this_thread::get_id());
    EXPECT_THAT(inner_.reports, IsEmpty());
}

}  // namespace
}  // namespace goldfish::grpc::v2
