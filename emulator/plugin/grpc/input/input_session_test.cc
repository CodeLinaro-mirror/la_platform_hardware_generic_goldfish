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

#include "input_session.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/test/fake_multi_display.h"
#include "goldfish/display/test/fake_pixman_display.h"
#include "standard-headers/linux/input-event-codes.h"
#include "standard-headers/linux/input.h"

// Necessary on Windows where winsock/windows headers define send as a macro.
#undef send

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::control::EvDevEvent;
using ::android::emulation::control::kMtsPointerUp;
using ::android::emulation::control::keyboard::IKeyEventSender;
using ::android::emulation::v2::input::AxisEvent;
using ::android::emulation::v2::input::AxisType;
using ::android::emulation::v2::input::ButtonMask;
using ::android::emulation::v2::input::InputEvent;
using ::android::emulation::v2::input::KeyAction;
using ::android::emulation::v2::input::KeyEvent;
using ::android::emulation::v2::input::PointerAction;
using ::android::emulation::v2::input::PointerEvent;
using ::android::emulation::v2::input::TextEvent;
using ::android::emulation::v2::input::ToolType;
using ::goldfish::async::EventLoop;
using ::goldfish::async::LibuvEventLoop;
using ::goldfish::async::ThreadedEventLoop;
using ::goldfish::display::test::ActiveFakePixmanDisplay;
using ::goldfish::display::test::FakeMultiDisplay;
using ::goldfish::display::test::FakePixmanDisplay;

class FakeTestKeyEventSender : public IKeyEventSender {
  public:
    void send(const ::android::emulation::control::KeyboardEvent request) override {
        sent_events.push_back(request);
    }

    std::vector<::android::emulation::control::KeyboardEvent> sent_events;
};

class InputSessionTestFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        loop_ = ThreadedEventLoop::Create(LibuvEventLoop::Create());
        multi_display_ = std::make_unique<FakeMultiDisplay>(loop_.get());
        key_sender_ = std::make_shared<FakeTestKeyEventSender>();
    }

    void TearDown() override {
        if (multi_display_) {
            for (auto display_weak : multi_display_->Displays()) {
                if (auto display = display_weak.lock()) {
                    static_cast<ActiveFakePixmanDisplay*>(display.get())->Stop();
                }
            }
        }
    }

    FakePixmanDisplay* GetDisplay(uint8_t id = 0) {
        auto res = multi_display_->GetActiveDisplay(id, false);
        if (!res.ok()) return nullptr;
        return static_cast<FakePixmanDisplay*>(res.value().get());
    }

    template <typename EventContainer>
    static bool HasEvent(const EventContainer& events, uint16_t type, uint16_t code,
                         std::optional<uint32_t> value = std::nullopt) {
        return std::any_of(events.begin(), events.end(), [&](const auto& ev) {
            return ev.type == type && ev.code == code && (!value.has_value() || ev.value == *value);
        });
    }

    std::unique_ptr<EventLoop> loop_;
    std::unique_ptr<FakeMultiDisplay> multi_display_;
    std::shared_ptr<FakeTestKeyEventSender> key_sender_;
};

TEST_F(InputSessionTestFixture, LifecycleCleanupReleasesHeldTouchesAndButtons) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    {
        InputSession session(*multi_display_, key_sender_, false);

        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);

        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(10);
        p1->set_x(0.2F);
        p1->set_y(0.4F);
        p1->set_pressure(0.8F);
        p1->set_button_mask(ButtonMask::BUTTON_MASK_PRIMARY | ButtonMask::BUTTON_MASK_SECONDARY);

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(20);
        p2->set_x(0.6F);
        p2->set_y(0.8F);
        p2->set_pressure(0.5F);

        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
        display->evdevs.clear();
    }

    // After session goes out of scope, verify all slots and buttons are released
    bool released_slot_10 = false;
    bool released_slot_20 = false;
    bool released_btn_primary = false;
    bool released_btn_secondary = false;
    bool had_syn_report = false;

    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == kMtsPointerUp) {
            released_slot_10 = true;
            released_slot_20 = true;
        }
        if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 0) {
            released_btn_primary = true;
        }
        if (ev.type == EV_KEY && ev.code == BTN_RIGHT && ev.value == 0) {
            released_btn_secondary = true;
        }
        if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            had_syn_report = true;
        }
    }

    EXPECT_TRUE(released_slot_10);
    EXPECT_TRUE(released_slot_20);
    EXPECT_TRUE(released_btn_primary);
    EXPECT_TRUE(released_btn_secondary);
    EXPECT_TRUE(had_syn_report);
}

TEST_F(InputSessionTestFixture, ExplicitCleanupIsIdempotent) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_x(0.5F);
    p->set_y(0.5F);
    p->set_pressure(1.0F);
    p->set_button_mask(ButtonMask::BUTTON_MASK_PRIMARY);

    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    display->evdevs.clear();
    session.Cleanup();
    EXPECT_FALSE(display->evdevs.empty());

    display->evdevs.clear();
    session.Cleanup();
    EXPECT_TRUE(display->evdevs.empty());
}

TEST_F(InputSessionTestFixture, MoveConstructorTransfersHeldState) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    std::unique_ptr<InputSession> session2;

    {
        InputSession session1(*multi_display_, key_sender_, false);

        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_x(0.5F);
        p->set_y(0.5F);
        p->set_pressure(1.0F);
        p->set_button_mask(ButtonMask::BUTTON_MASK_PRIMARY);

        EXPECT_TRUE(session1.DispatchInputEvent(event).ok());
        display->evdevs.clear();

        session2 = std::make_unique<InputSession>(std::move(session1));
        // session1 destroyed here
    }

    // session1 destruction should not have triggered cleanup since state was moved
    EXPECT_TRUE(display->evdevs.empty());

    // Destroy session2
    session2.reset();

    // Now cleanup should have executed
    bool found_touch_up = false;
    bool found_btn_release = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == kMtsPointerUp) {
            found_touch_up = true;
        }
        if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 0) {
            found_btn_release = true;
        }
    }
    EXPECT_TRUE(found_touch_up);
    EXPECT_TRUE(found_btn_release);
}

TEST_F(InputSessionTestFixture, MoveAssignmentTransfersHeldState) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session1(*multi_display_, key_sender_, false);
    InputSession session2(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_x(0.5F);
    p->set_y(0.5F);
    p->set_pressure(1.0F);
    p->set_button_mask(ButtonMask::BUTTON_MASK_PRIMARY);

    EXPECT_TRUE(session1.DispatchInputEvent(event).ok());
    display->evdevs.clear();

    session2 = std::move(session1);
    EXPECT_TRUE(display->evdevs.empty());

    session2.Cleanup();
    EXPECT_FALSE(display->evdevs.empty());
}

TEST_F(InputSessionTestFixture, SelfMoveAssignmentIsSafe) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_x(0.5F);
    p->set_y(0.5F);
    p->set_pressure(1.0F);
    p->set_button_mask(ButtonMask::BUTTON_MASK_PRIMARY);

    EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    display->evdevs.clear();

    // Suppress clang self-move warning for testing robustness
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
    session = std::move(session);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    // Self-move should be a no-op and not trigger premature cleanup
    EXPECT_TRUE(display->evdevs.empty());

    session.Cleanup();
    EXPECT_FALSE(display->evdevs.empty());
}

TEST_F(InputSessionTestFixture, MultiTouch_SingleFingerDownTracking) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(100);
    p->set_x(0.25F);
    p->set_y(0.50F);
    p->set_pressure(0.75F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_slot_0 = false;
    bool found_tracking_0 = false;
    bool found_pos_x = false;
    bool found_pos_y = false;
    bool found_pressure = false;

    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT && ev.value == 0) found_slot_0 = true;
        if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == 0)
            found_tracking_0 = true;
        if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X &&
            ev.value == static_cast<uint32_t>(0.25F * 0x7FFF))
            found_pos_x = true;
        if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y &&
            ev.value == static_cast<uint32_t>(0.50F * 0x7FFF))
            found_pos_y = true;
        if (ev.type == EV_ABS && ev.code == ABS_MT_PRESSURE &&
            ev.value == static_cast<uint32_t>(0.75F * 0x7FFF))
            found_pressure = true;
    }

    EXPECT_TRUE(found_slot_0);
    EXPECT_TRUE(found_tracking_0);
    EXPECT_TRUE(found_pos_x);
    EXPECT_TRUE(found_pos_y);
    EXPECT_TRUE(found_pressure);
}

TEST_F(InputSessionTestFixture, MultiTouch_TwoFingersSimultaneousTracking) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    // Finger 1 down
    {
        InputEvent down_event;
        auto* ptr = down_event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(100);
        p->set_x(0.25F);
        p->set_y(0.50F);
        p->set_pressure(0.75F);
        EXPECT_TRUE(session.DispatchInputEvent(down_event).ok());
    }

    // Finger 2 down
    {
        InputEvent move_event;
        auto* ptr = move_event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);

        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(100);
        p1->set_x(0.30F);
        p1->set_y(0.50F);
        p1->set_pressure(0.75F);

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(200);
        p2->set_x(0.75F);
        p2->set_y(0.80F);
        p2->set_pressure(0.90F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(move_event).ok());

        bool found_slot_1 = false;
        bool found_tracking_1 = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT && ev.value == 1) found_slot_1 = true;
            if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == 1)
                found_tracking_1 = true;
        }
        EXPECT_TRUE(found_slot_1);
        EXPECT_TRUE(found_tracking_1);
    }
}

TEST_F(InputSessionTestFixture, MultiTouch_SingleFingerReleaseMaintainsRemainingTouch) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    // Initial 2 fingers down
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(100);
        p1->set_x(0.30F);
        p1->set_y(0.50F);
        p1->set_pressure(0.75F);

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(200);
        p2->set_x(0.75F);
        p2->set_y(0.80F);
        p2->set_pressure(0.90F);
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // Finger 1 lifts (pressure 0.0F)
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);

        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(100);
        p1->set_pressure(0.0F);  // Up indicator

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(200);
        p2->set_x(0.75F);
        p2->set_y(0.80F);
        p2->set_pressure(0.90F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_slot_0_up = false;
        for (size_t i = 0; i + 1 < display->evdevs.size(); ++i) {
            if (display->evdevs[i].type == EV_ABS && display->evdevs[i].code == ABS_MT_SLOT &&
                display->evdevs[i].value == 0 && display->evdevs[i + 1].type == EV_ABS &&
                display->evdevs[i + 1].code == ABS_MT_TRACKING_ID &&
                display->evdevs[i + 1].value == kMtsPointerUp) {
                found_slot_0_up = true;
            }
        }
        EXPECT_TRUE(found_slot_0_up);
    }
}

TEST_F(InputSessionTestFixture, MultiDisplayRouting) {
    auto disp1_res = multi_display_->CreateDisplay(1, 1080, 1920, 320, 0);
    ASSERT_TRUE(disp1_res.ok());

    auto* display0 = GetDisplay(0);
    auto* display1 = GetDisplay(1);
    ASSERT_NE(display0, nullptr);
    ASSERT_NE(display1, nullptr);

    {
        InputSession session(*multi_display_, key_sender_, false);

        // Send to display 0
        InputEvent event0;
        auto* ptr0 = event0.mutable_pointer();
        ptr0->set_display_id(0);
        ptr0->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p0 = ptr0->add_pointers();
        p0->set_pointer_id(1);
        p0->set_x(0.1F);
        p0->set_y(0.1F);
        p0->set_pressure(1.0F);

        display0->evdevs.clear();
        display1->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event0).ok());
        EXPECT_FALSE(display0->evdevs.empty());
        EXPECT_TRUE(display1->evdevs.empty());

        // Send to display 1
        InputEvent event1;
        auto* ptr1 = event1.mutable_pointer();
        ptr1->set_display_id(1);
        ptr1->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p1 = ptr1->add_pointers();
        p1->set_pointer_id(2);
        p1->set_x(0.9F);
        p1->set_y(0.9F);
        p1->set_pressure(1.0F);

        display0->evdevs.clear();
        display1->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event1).ok());
        EXPECT_TRUE(display0->evdevs.empty());
        EXPECT_FALSE(display1->evdevs.empty());

        display0->evdevs.clear();
        display1->evdevs.clear();
    }

    // Cleanup on destruction flushes to both display0 and display1
    EXPECT_FALSE(display0->evdevs.empty());
    EXPECT_FALSE(display1->evdevs.empty());
}

TEST_F(InputSessionTestFixture, ToolTypes_StylusEmitsPenToolAndTilt) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_x(0.5F);
    p->set_y(0.5F);
    p->set_pressure(1.0F);
    p->set_size(0.4F);
    p->set_orientation_radians(0.785398F);  // ~45 degrees (pi / 4)
    p->set_tilt_radians(0.523599F);         // ~30 degrees (pi / 6)
    p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_tool_pen = false;
    bool found_touch_major = false;
    bool found_orientation_45 = false;
    bool found_tilt_x = false;
    bool found_tilt_y = false;

    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_TOOL_TYPE && ev.value == MT_TOOL_PEN) {
            found_tool_pen = true;
        }
        if (ev.type == EV_ABS && ev.code == ABS_MT_TOUCH_MAJOR &&
            ev.value == static_cast<uint32_t>(0.4F * 0x7FFF)) {
            found_touch_major = true;
        }
        if (ev.type == EV_ABS && ev.code == ABS_MT_ORIENTATION && ev.value == 45) {
            found_orientation_45 = true;
        }
        if (ev.type == EV_ABS && ev.code == ABS_TILT_X && ev.value == 21) {
            found_tilt_x = true;
        }
        if (ev.type == EV_ABS && ev.code == ABS_TILT_Y && ev.value == static_cast<uint32_t>(-21)) {
            found_tilt_y = true;
        }
    }

    EXPECT_TRUE(found_tool_pen);
    EXPECT_TRUE(found_touch_major);
    EXPECT_TRUE(found_orientation_45);
    EXPECT_TRUE(found_tilt_x);
    EXPECT_TRUE(found_tilt_y);
}

TEST_F(InputSessionTestFixture, ToolTypes_OrientationPositiveAndNegative) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_x(0.5F);
    p->set_y(0.5F);
    p->set_pressure(1.0F);
    p->set_orientation_radians(-0.785398F);  // -45 degrees

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_orientation_neg_45 = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_ORIENTATION &&
            ev.value == static_cast<uint32_t>(-45)) {
            found_orientation_neg_45 = true;
        }
    }
    EXPECT_TRUE(found_orientation_neg_45);
}

TEST_F(InputSessionTestFixture, RelativeMotionMouseButtonsAndScrolling) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    // Relative mouse move + scroll + back button
    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_RELATIVE_MOVE);
    ptr->set_scroll_x(3.0F);
    ptr->set_scroll_y(-5.0F);

    auto* p = ptr->add_pointers();
    p->set_pointer_id(0);
    p->set_delta_x(12.0F);
    p->set_delta_y(-8.0F);
    p->set_button_mask(ButtonMask::BUTTON_MASK_BACK);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_rel_x = false;
    bool found_rel_y = false;
    bool found_rel_hwheel = false;
    bool found_rel_wheel = false;
    bool found_btn_side = false;

    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_REL && ev.code == REL_X && ev.value == 12) found_rel_x = true;
        if (ev.type == EV_REL && ev.code == REL_Y && ev.value == static_cast<uint32_t>(-8))
            found_rel_y = true;
        if (ev.type == EV_REL && ev.code == REL_HWHEEL && ev.value == 3) found_rel_hwheel = true;
        if (ev.type == EV_REL && ev.code == REL_WHEEL && ev.value == static_cast<uint32_t>(-5))
            found_rel_wheel = true;
        if (ev.type == EV_KEY && ev.code == BTN_SIDE && ev.value == 1) found_btn_side = true;
    }

    EXPECT_TRUE(found_rel_x);
    EXPECT_TRUE(found_rel_y);
    EXPECT_TRUE(found_rel_hwheel);
    EXPECT_TRUE(found_rel_wheel);
    EXPECT_TRUE(found_btn_side);
}

TEST_F(InputSessionTestFixture, KeyEvents_DomCodeMappedToKeyDown) {
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* key = event.mutable_key();
    key->set_action(KeyAction::KEY_ACTION_DOWN);
    key->set_dom_code("KeyA");

    key_sender_->sent_events.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    ASSERT_EQ(key_sender_->sent_events.size(), 1);
    EXPECT_EQ(key_sender_->sent_events[0].key(), "KeyA");
    EXPECT_EQ(key_sender_->sent_events[0].eventtype(),
              ::android::emulation::control::KeyboardEvent::keydown);
}

TEST_F(InputSessionTestFixture, KeyEvents_AndroidKeycodeMappedToEvdevKey) {
    InputSession session(*multi_display_, key_sender_, false);

    // Android Keycode HOME (3) -> Evdev KEY_HOMEPAGE (172)
    InputEvent event;
    auto* key = event.mutable_key();
    key->set_action(KeyAction::KEY_ACTION_UP);
    key->set_android_keycode(3);

    key_sender_->sent_events.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    ASSERT_EQ(key_sender_->sent_events.size(), 1);
    EXPECT_EQ(key_sender_->sent_events[0].keycode(), KEY_HOMEPAGE);
    EXPECT_EQ(key_sender_->sent_events[0].codetype(),
              ::android::emulation::control::KeyboardEvent::Evdev);
    EXPECT_EQ(key_sender_->sent_events[0].eventtype(),
              ::android::emulation::control::KeyboardEvent::keyup);
}

TEST_F(InputSessionTestFixture, KeyEvents_RawEvdevCodePassthrough) {
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* key = event.mutable_key();
    key->set_action(KeyAction::KEY_ACTION_PRESS);
    key->set_evdev_code(KEY_POWER);

    key_sender_->sent_events.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    ASSERT_EQ(key_sender_->sent_events.size(), 1);
    EXPECT_EQ(key_sender_->sent_events[0].keycode(), KEY_POWER);
}

TEST_F(InputSessionTestFixture, TextEvents_DispatchesNonEmptyText) {
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* text = event.mutable_text();
    text->set_text("Hello Android 🤖");

    key_sender_->sent_events.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    ASSERT_EQ(key_sender_->sent_events.size(), 1);
    EXPECT_EQ(key_sender_->sent_events[0].text(), "Hello Android 🤖");
}

TEST_F(InputSessionTestFixture, TextEvents_IgnoresEmptyText) {
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent empty_event;
    empty_event.mutable_text()->set_text("");
    key_sender_->sent_events.clear();
    EXPECT_TRUE(session.DispatchInputEvent(empty_event).ok());
    EXPECT_TRUE(key_sender_->sent_events.empty());
}

TEST_F(InputSessionTestFixture, AxisRotaryEvents) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);

    InputSession session(*multi_display_, key_sender_, false);

    // Rotary encoder +2 ticks
    InputEvent event;
    auto* axis = event.mutable_axis();
    axis->set_axis(AxisType::AXIS_TYPE_ROTARY_ENCODER);
    axis->set_value(2.0F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_wheel = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_REL && ev.code == REL_WHEEL && ev.value == 2) {
            found_wheel = true;
        }
    }
    EXPECT_TRUE(found_wheel);
}

TEST_F(InputSessionTestFixture, InvalidDisplayReturnsErrorStatus) {
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(999);  // Non-existent display
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_pressure(1.0F);

    auto status = session.DispatchInputEvent(event);
    EXPECT_FALSE(status.ok());
}

TEST_F(InputSessionTestFixture, NullKeyEventSenderGracefulHandling) {
    InputSession session(*multi_display_, nullptr, false);

    InputEvent key_event;
    key_event.mutable_key()->set_dom_code("KeyA");
    EXPECT_TRUE(session.DispatchInputEvent(key_event).ok());

    InputEvent text_event;
    text_event.mutable_text()->set_text("Hello");
    EXPECT_TRUE(session.DispatchInputEvent(text_event).ok());
}

TEST_F(InputSessionTestFixture, UnsetEventCaseReturnsOk) {
    InputSession session(*multi_display_, key_sender_, false);
    InputEvent empty_event;
    EXPECT_TRUE(session.DispatchInputEvent(empty_event).ok());
}

// ============================================================================
// Libevdev-Inspired Multi-Touch Protocol B Validation Suite
// ============================================================================

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_SingleFingerDownAllocatesSlot0) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(10);
    p->set_x(0.2F);
    p->set_y(0.3F);
    p->set_pressure(0.5F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_slot_0 = false;
    bool found_tracking_0 = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT && ev.value == 0) found_slot_0 = true;
        if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == 0)
            found_tracking_0 = true;
    }
    EXPECT_TRUE(found_slot_0);
    EXPECT_TRUE(found_tracking_0);
    ASSERT_FALSE(display->evdevs.empty());
    EXPECT_EQ(display->evdevs.back().type, EV_SYN);
    EXPECT_EQ(display->evdevs.back().code, SYN_REPORT);
}

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_SecondFingerDownAllocatesSlot1) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // 1. Finger 10 Down (Allocates Slot 0)
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(10);
        p->set_x(0.2F);
        p->set_y(0.3F);
        p->set_pressure(0.5F);
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // 2. Finger 20 Down (Allocates Slot 1)
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(10);
        p1->set_x(0.22F);
        p1->set_y(0.32F);
        p1->set_pressure(0.5F);

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(20);
        p2->set_x(0.7F);
        p2->set_y(0.8F);
        p2->set_pressure(0.6F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_slot_1 = false;
        bool found_tracking_1 = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT && ev.value == 1) found_slot_1 = true;
            if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == 1)
                found_tracking_1 = true;
        }
        EXPECT_TRUE(found_slot_1);
        EXPECT_TRUE(found_tracking_1);
    }
}

TEST_F(InputSessionTestFixture,
       Libevdev_MultiTouchProtocolB_ReleasingFirstFingerEmitsPointerUpOnSlot0) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // Initial two fingers down
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(10);
        p1->set_x(0.2F);
        p1->set_y(0.3F);
        p1->set_pressure(0.5F);

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(20);
        p2->set_x(0.7F);
        p2->set_y(0.8F);
        p2->set_pressure(0.6F);
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // Finger 10 Up (Releases Slot 0 while Finger 20 remains active)
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(10);
        p1->set_pressure(0.0F);  // Finger 10 lifted

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(20);
        p2->set_x(0.75F);
        p2->set_y(0.85F);
        p2->set_pressure(0.6F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_slot_0_release = false;
        for (size_t i = 0; i + 1 < display->evdevs.size(); ++i) {
            if (display->evdevs[i].type == EV_ABS && display->evdevs[i].code == ABS_MT_SLOT &&
                display->evdevs[i].value == 0 && display->evdevs[i + 1].type == EV_ABS &&
                display->evdevs[i + 1].code == ABS_MT_TRACKING_ID &&
                display->evdevs[i + 1].value == kMtsPointerUp) {
                found_slot_0_release = true;
            }
        }
        EXPECT_TRUE(found_slot_0_release);
    }
}

TEST_F(InputSessionTestFixture,
       Libevdev_MultiTouchProtocolB_ReleasingSecondFingerEmitsPointerUpOnSlot1) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // Initial two fingers down
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(10);
        p1->set_x(0.2F);
        p1->set_y(0.3F);
        p1->set_pressure(0.5F);

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(20);
        p2->set_x(0.7F);
        p2->set_y(0.8F);
        p2->set_pressure(0.6F);
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // Finger 10 lifted
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(10);
        p1->set_pressure(0.0F);
        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(20);
        p2->set_x(0.75F);
        p2->set_y(0.85F);
        p2->set_pressure(0.6F);
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // Finger 20 Up (Releases Slot 1)
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_UP);
        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(20);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_slot_1_release = false;
        for (size_t i = 0; i + 1 < display->evdevs.size(); ++i) {
            if (display->evdevs[i].type == EV_ABS && display->evdevs[i].code == ABS_MT_SLOT &&
                display->evdevs[i].value == 1 && display->evdevs[i + 1].type == EV_ABS &&
                display->evdevs[i + 1].code == ABS_MT_TRACKING_ID &&
                display->evdevs[i + 1].value == kMtsPointerUp) {
                found_slot_1_release = true;
            }
        }
        EXPECT_TRUE(found_slot_1_release);
    }
}

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_SlotRecyclingAndTrackingIds) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // Touch down on finger 1 -> Slot 0
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_x(0.1F);
        p->set_y(0.1F);
        p->set_pressure(0.5F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // Lift finger 1 -> Slot 0 freed
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_UP);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // New touch on finger 2 -> Reclaims Slot 0 with fresh tracking ID
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(2);
        p->set_x(0.5F);
        p->set_y(0.5F);
        p->set_pressure(0.8F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_recycled_slot_0 = false;
        bool found_fresh_tracking_id = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT && ev.value == 0) {
                found_recycled_slot_0 = true;
            }
            if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == 0) {
                found_fresh_tracking_id = true;
            }
        }
        EXPECT_TRUE(found_recycled_slot_0);
        EXPECT_TRUE(found_fresh_tracking_id);
    }
}

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_OutOfOrderSlotRelease) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // Fingers 1, 2, 3 down -> Slots 0, 1, 2
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        for (int i = 1; i <= 3; ++i) {
            auto* p = ptr->add_pointers();
            p->set_pointer_id(i * 100);
            p->set_x(0.1F * i);
            p->set_y(0.1F * i);
            p->set_pressure(0.5F);
        }
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // Release Middle Finger (Finger 200 / Slot 1) out of order
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
        auto* p1 = ptr->add_pointers();
        p1->set_pointer_id(100);
        p1->set_x(0.12F);
        p1->set_y(0.12F);
        p1->set_pressure(0.5F);

        auto* p2 = ptr->add_pointers();
        p2->set_pointer_id(200);
        p2->set_pressure(0.0F);  // Release

        auto* p3 = ptr->add_pointers();
        p3->set_pointer_id(300);
        p3->set_x(0.32F);
        p3->set_y(0.32F);
        p3->set_pressure(0.5F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_slot_1_up = false;
        for (size_t i = 0; i + 1 < display->evdevs.size(); ++i) {
            if (display->evdevs[i].type == EV_ABS && display->evdevs[i].code == ABS_MT_SLOT &&
                display->evdevs[i].value == 1 && display->evdevs[i + 1].type == EV_ABS &&
                display->evdevs[i + 1].code == ABS_MT_TRACKING_ID &&
                display->evdevs[i + 1].value == kMtsPointerUp) {
                found_slot_1_up = true;
            }
        }
        EXPECT_TRUE(found_slot_1_up);
    }
}

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_NoTrackingIdOnMove) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // 1. Touch Down
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(42);
        p->set_x(0.2F);
        p->set_y(0.2F);
        p->set_pressure(0.5F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
        bool had_tracking = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) had_tracking = true;
        }
        EXPECT_TRUE(had_tracking);
    }

    // 2. Successive Moves MUST NOT emit ABS_MT_TRACKING_ID
    for (int step = 1; step <= 5; ++step) {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(42);
        p->set_x(0.2F + 0.05F * step);
        p->set_y(0.2F + 0.05F * step);
        p->set_pressure(0.5F);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        for (const auto& ev : display->evdevs) {
            EXPECT_NE(ev.code, ABS_MT_TRACKING_ID)
                    << "Protocol B violation: ABS_MT_TRACKING_ID must not be emitted on MOVE";
        }
    }
}

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_TenFingerSimultaneousContacts) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // Touch down 10 fingers simultaneously
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        for (int i = 0; i < 10; ++i) {
            auto* p = ptr->add_pointers();
            p->set_pointer_id(1000 + i);
            p->set_x(0.05F * i);
            p->set_y(0.05F * i);
            p->set_pressure(0.5F);
        }

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        std::set<uint32_t> tracked_slots;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) {
                tracked_slots.insert(ev.value);
            }
        }
        EXPECT_EQ(tracked_slots.size(), 10U);
    }
}

TEST_F(InputSessionTestFixture, Libevdev_RelativeMotion_Signed32BitDeltas) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_RELATIVE_MOVE);
    ptr->set_scroll_x(-120.0F);
    ptr->set_scroll_y(-240.0F);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(0);
    p->set_delta_x(-1024.0F);
    p->set_delta_y(-2048.0F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_neg_rel_x = false;
    bool found_neg_rel_y = false;
    bool found_neg_wheel_x = false;
    bool found_neg_wheel_y = false;

    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_REL && ev.code == REL_X && ev.value == static_cast<uint32_t>(-1024))
            found_neg_rel_x = true;
        if (ev.type == EV_REL && ev.code == REL_Y && ev.value == static_cast<uint32_t>(-2048))
            found_neg_rel_y = true;
        if (ev.type == EV_REL && ev.code == REL_HWHEEL && ev.value == static_cast<uint32_t>(-120))
            found_neg_wheel_x = true;
        if (ev.type == EV_REL && ev.code == REL_WHEEL && ev.value == static_cast<uint32_t>(-240))
            found_neg_wheel_y = true;
    }

    EXPECT_TRUE(found_neg_rel_x);
    EXPECT_TRUE(found_neg_rel_y);
    EXPECT_TRUE(found_neg_wheel_x);
    EXPECT_TRUE(found_neg_wheel_y);
}

TEST_F(InputSessionTestFixture, Libevdev_Evdev_FrameDelimitersPerBatch) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_x(0.5F);
    p->set_y(0.5F);
    p->set_pressure(0.5F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    ASSERT_FALSE(display->evdevs.empty());
    EXPECT_EQ(display->evdevs.back().type, EV_SYN);
    EXPECT_EQ(display->evdevs.back().code, SYN_REPORT);
    EXPECT_EQ(display->evdevs.back().value, 0U);
}

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_SlotCapacityExhaustionRejection) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // 1. Fill all 10 available slots (kMtsPointersNum = 10)
    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    for (int i = 1; i <= 10; ++i) {
        auto* p = ptr->add_pointers();
        p->set_pointer_id(i);
        p->set_x(0.05F * i);
        p->set_y(0.05F * i);
        p->set_pressure(0.5F);
    }
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    // 2. Attempt to add an 11th finger when all slots are full
    InputEvent overflow_event;
    auto* overflow_ptr = overflow_event.mutable_pointer();
    overflow_ptr->set_display_id(0);
    overflow_ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
    // Keep existing 10
    for (int i = 1; i <= 10; ++i) {
        auto* p = overflow_ptr->add_pointers();
        p->set_pointer_id(i);
        p->set_x(0.05F * i);
        p->set_y(0.05F * i);
        p->set_pressure(0.5F);
    }
    // Add 11th finger
    auto* p11 = overflow_ptr->add_pointers();
    p11->set_pointer_id(11);
    p11->set_x(0.99F);
    p11->set_y(0.99F);
    p11->set_pressure(0.5F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(overflow_event).ok());

    // Verify 11th finger did not emit slot >= 10
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) {
            EXPECT_LT(ev.value, 10U);
        }
    }
}

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_CoordinateClampingMinMax) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    // Out-of-bounds coordinates (<0 clamped to 0, >1 clamped to 0x7FFF)
    p->set_x(-0.5F);
    p->set_y(1.5F);
    p->set_pressure(2.0F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_clamped_x = false;
    bool found_clamped_y = false;
    bool found_clamped_pressure = false;

    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X && ev.value == 0)
            found_clamped_x = true;
        if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y && ev.value == 0x7FFF)
            found_clamped_y = true;
        if (ev.type == EV_ABS && ev.code == ABS_MT_PRESSURE && ev.value == 0x7FFF)
            found_clamped_pressure = true;
    }

    EXPECT_TRUE(found_clamped_x);
    EXPECT_TRUE(found_clamped_y);
    EXPECT_TRUE(found_clamped_pressure);
}

TEST_F(InputSessionTestFixture, Libevdev_MultiTouchProtocolB_OrientationClampingLimits) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_x(0.5F);
    p->set_y(0.5F);
    p->set_pressure(0.5F);
    p->set_orientation_radians(3.14159F);  // π radians (> 90 deg -> clamp to 90)

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_clamped_orientation = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_ORIENTATION && ev.value == 90)
            found_clamped_orientation = true;
    }
    EXPECT_TRUE(found_clamped_orientation);

    // Test negative clamp (< -90 deg -> clamp to -90)
    p->set_orientation_radians(-3.14159F);
    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_neg_clamped_orientation = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_ORIENTATION &&
            ev.value == static_cast<uint32_t>(-90))
            found_neg_clamped_orientation = true;
    }
    EXPECT_TRUE(found_neg_clamped_orientation);
}

TEST_F(InputSessionTestFixture, Libevdev_Stylus_HoverMoveEmitsPenToolType) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_HOVER_MOVE);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
    p->set_x(0.3F);
    p->set_y(0.3F);
    p->set_pressure(0.0F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_pen_tool = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_TOOL_TYPE && ev.value == MT_TOOL_PEN) {
            found_pen_tool = true;
        }
    }
    EXPECT_TRUE(found_pen_tool);
}

TEST_F(InputSessionTestFixture, Libevdev_Stylus_TouchDownEmitsTrackingAndTilt) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
    p->set_x(0.3F);
    p->set_y(0.3F);
    p->set_pressure(0.7F);
    p->set_tilt_radians(0.5F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_tracking = false;
    bool found_tilt_x = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == 0)
            found_tracking = true;
        if (ev.type == EV_ABS && ev.code == ABS_TILT_X) found_tilt_x = true;
    }
    EXPECT_TRUE(found_tracking);
    EXPECT_TRUE(found_tilt_x);
}

TEST_F(InputSessionTestFixture, Libevdev_Stylus_LiftEmitsPointerUp) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // Touch down first
    {
        InputEvent down_event;
        auto* ptr = down_event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
        p->set_x(0.3F);
        p->set_y(0.3F);
        p->set_pressure(0.7F);
        EXPECT_TRUE(session.DispatchInputEvent(down_event).ok());
    }

    // Lift stylus
    {
        InputEvent up_event;
        auto* ptr = up_event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_UP);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(up_event).ok());

        bool found_tracking_up = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value == kMtsPointerUp) {
                found_tracking_up = true;
            }
        }
        EXPECT_TRUE(found_tracking_up);
    }
}

TEST_F(InputSessionTestFixture, Libevdev_Stylus_EraserTransducerEmitsEvents) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(2);
    p->set_tool_type(ToolType::TOOL_TYPE_ERASER);
    p->set_x(0.4F);
    p->set_y(0.4F);
    p->set_pressure(0.8F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_rubber_tool = false;
    bool found_tracking = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_TOOL_TYPE && ev.value == MT_TOOL_PEN) {
            found_rubber_tool = true;
        }
        if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) {
            found_tracking = true;
        }
    }
    EXPECT_TRUE(found_rubber_tool);
    EXPECT_TRUE(found_tracking);
}

TEST_F(InputSessionTestFixture, Libevdev_MouseButtons_PrimaryButtonDown) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_button_mask(ButtonMask::BUTTON_MASK_PRIMARY);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_btn_left_down = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1) found_btn_left_down = true;
    }
    EXPECT_TRUE(found_btn_left_down);
}

TEST_F(InputSessionTestFixture, Libevdev_MouseButtons_SimultaneousChordingAndPartialRelease) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // 1. Primary Button Down
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_button_mask(ButtonMask::BUTTON_MASK_PRIMARY);
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());
    }

    // 2. Chording: Secondary + Tertiary (Middle) Down while Primary is still held
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_button_mask(ButtonMask::BUTTON_MASK_PRIMARY | ButtonMask::BUTTON_MASK_SECONDARY |
                           ButtonMask::BUTTON_MASK_TERTIARY);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_btn_right_down = false;
        bool found_btn_middle_down = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_KEY && ev.code == BTN_RIGHT && ev.value == 1)
                found_btn_right_down = true;
            if (ev.type == EV_KEY && ev.code == BTN_MIDDLE && ev.value == 1)
                found_btn_middle_down = true;
        }
        EXPECT_TRUE(found_btn_right_down);
        EXPECT_TRUE(found_btn_middle_down);
    }

    // 3. Release Primary & Middle, keeping Secondary held
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_button_mask(ButtonMask::BUTTON_MASK_SECONDARY);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_btn_left_up = false;
        bool found_btn_middle_up = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 0) found_btn_left_up = true;
            if (ev.type == EV_KEY && ev.code == BTN_MIDDLE && ev.value == 0)
                found_btn_middle_up = true;
        }
        EXPECT_TRUE(found_btn_left_up);
        EXPECT_TRUE(found_btn_middle_up);
    }
}

// ============================================================================
// Libinput-Inspired Tablet and Stylus Validation Suite
// ============================================================================

TEST_F(InputSessionTestFixture, Libinput_Tablet_StylusPrimaryBarrelButtonDuringContact) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // 1. Stylus contact down with primary barrel button pressed
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
        p->set_x(0.35F);
        p->set_y(0.45F);
        p->set_pressure(0.6F);
        p->set_button_mask(ButtonMask::BUTTON_MASK_STYLUS_PRIMARY);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_btn_stylus_down = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_KEY && ev.code == BTN_STYLUS && ev.value == 1) {
                found_btn_stylus_down = true;
            }
        }
        EXPECT_TRUE(found_btn_stylus_down);
    }

    // 2. Release barrel button while keeping stylus tip in contact
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_MOVE);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
        p->set_x(0.36F);
        p->set_y(0.46F);
        p->set_pressure(0.6F);
        p->set_button_mask(0);  // Released

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_btn_stylus_up = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_KEY && ev.code == BTN_STYLUS && ev.value == 0) {
                found_btn_stylus_up = true;
            }
        }
        EXPECT_TRUE(found_btn_stylus_up);
    }
}

TEST_F(InputSessionTestFixture, Libinput_Tablet_StylusSecondaryBarrelButtonDuringHover) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // 1. Stylus hovering with secondary barrel button pressed
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_HOVER_MOVE);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
        p->set_x(0.5F);
        p->set_y(0.5F);
        p->set_pressure(0.0F);
        p->set_button_mask(ButtonMask::BUTTON_MASK_STYLUS_SECONDARY);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_btn_stylus2_down = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_KEY && ev.code == BTN_STYLUS2 && ev.value == 1) {
                found_btn_stylus2_down = true;
            }
        }
        EXPECT_TRUE(found_btn_stylus2_down);
    }

    // 2. Release secondary barrel button while continuing hover
    {
        InputEvent event;
        auto* ptr = event.mutable_pointer();
        ptr->set_display_id(0);
        ptr->set_action(PointerAction::POINTER_ACTION_HOVER_MOVE);
        auto* p = ptr->add_pointers();
        p->set_pointer_id(1);
        p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
        p->set_x(0.52F);
        p->set_y(0.52F);
        p->set_pressure(0.0F);
        p->set_button_mask(0);

        display->evdevs.clear();
        EXPECT_TRUE(session.DispatchInputEvent(event).ok());

        bool found_btn_stylus2_up = false;
        for (const auto& ev : display->evdevs) {
            if (ev.type == EV_KEY && ev.code == BTN_STYLUS2 && ev.value == 0) {
                found_btn_stylus2_up = true;
            }
        }
        EXPECT_TRUE(found_btn_stylus2_up);
    }
}

TEST_F(InputSessionTestFixture, Libinput_Tablet_StylusDualBarrelButtonsChording) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
    p->set_x(0.2F);
    p->set_y(0.4F);
    p->set_pressure(0.7F);
    p->set_tilt_radians(0.3F);
    p->set_button_mask(ButtonMask::BUTTON_MASK_STYLUS_PRIMARY |
                       ButtonMask::BUTTON_MASK_STYLUS_SECONDARY);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    bool found_btn_stylus = false;
    bool found_btn_stylus2 = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == EV_KEY && ev.code == BTN_STYLUS && ev.value == 1) found_btn_stylus = true;
        if (ev.type == EV_KEY && ev.code == BTN_STYLUS2 && ev.value == 1) found_btn_stylus2 = true;
    }
    EXPECT_TRUE(found_btn_stylus);
    EXPECT_TRUE(found_btn_stylus2);
}

TEST_F(InputSessionTestFixture, Libinput_Tablet_PalmToolTypeEmitsMtToolPalm) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(5);
    p->set_tool_type(ToolType::TOOL_TYPE_PALM);
    p->set_x(0.6F);
    p->set_y(0.7F);
    p->set_pressure(0.9F);
    p->set_size(0.8F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    EXPECT_TRUE(HasEvent(display->evdevs, EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM));
}

TEST_F(InputSessionTestFixture, DownWithDefaultZeroPressureAllocatesSlotAndEmitsContact) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    // Client sends DOWN with default/omitted pressure (0.0F in Proto3)
    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(42);
    p->set_x(0.3F);
    p->set_y(0.7F);
    p->set_pressure(0.0F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    // Verify slot was allocated, tracking ID was assigned, and default contact pressure was emitted
    EXPECT_TRUE(HasEvent(display->evdevs, EV_ABS, ABS_MT_SLOT, 0));
    EXPECT_TRUE(HasEvent(display->evdevs, EV_ABS, ABS_MT_TRACKING_ID, 0));
    EXPECT_TRUE(HasEvent(display->evdevs, EV_ABS, ABS_MT_PRESSURE, 0x7FFF));
}

TEST_F(InputSessionTestFixture, HoverMoveEmitsZeroPressure) {
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* ptr = event.mutable_pointer();
    ptr->set_display_id(0);
    ptr->set_action(PointerAction::POINTER_ACTION_HOVER_MOVE);
    auto* p = ptr->add_pointers();
    p->set_pointer_id(1);
    p->set_tool_type(ToolType::TOOL_TYPE_STYLUS);
    p->set_x(0.4F);
    p->set_y(0.6F);
    p->set_pressure(0.0F);

    display->evdevs.clear();
    EXPECT_TRUE(session.DispatchInputEvent(event).ok());

    // Hover must emit ABS_MT_PRESSURE = 0
    EXPECT_TRUE(HasEvent(display->evdevs, EV_ABS, ABS_MT_PRESSURE, 0));
}

TEST_F(InputSessionTestFixture, UnmappedAndroidKeycodeReturnsInvalidArgumentError) {
    InputSession session(*multi_display_, key_sender_, false);

    InputEvent event;
    auto* key = event.mutable_key();
    key->set_action(KeyAction::KEY_ACTION_DOWN);
    key->set_android_keycode(9999);  // Non-existent Android keycode

    key_sender_->sent_events.clear();
    EXPECT_FALSE(session.DispatchInputEvent(event).ok());
    EXPECT_TRUE(key_sender_->sent_events.empty());
}

}  // namespace
}  // namespace goldfish::grpc::v2
