// Copyright (C) 2025 The Android Open Source Project
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
#include "android/emulation/control/input/PointerEventDispatcher.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "absl/time/time.h"
#include "gmock/gmock.h"

#include "android/emulation/control/input/SlotRegistry.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/display/test/MockDisplay.h"
#include "standard-headers/linux/input-event-codes.h"
#include "standard-headers/linux/input.h"

using android::emulation::control::EvDevEvent;
using ::goldfish::async::testing::TestEventLoop;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::Mock;

namespace android {
namespace emulation {
namespace control {

// Helper function to print EvDevEvent objects in case of assertion failure.
void PrintTo(const android::emulation::control::EvDevEvent& event, std::ostream* os) {
    *os << "{type: " << std::hex << event.type << ", code: " << event.code
        << ", value: " << event.value << std::dec << "}";
}

namespace internal {

using ::goldfish::display::test::MockDisplay;
using namespace std::chrono_literals;

TEST(PenTouchEventTest, EvDevEventsConversion) {
    SlotRegistry registry;
    PenTouchEvent event;
    Pen pen;
    pen.button_pressed = true;
    pen.rubber_pointer = true;
    pen.x = 1;
    pen.y = 2;
    pen.identifier = 0x0a;
    pen.pressure = 50;
    pen.touch_major = 0;
    pen.touch_minor = 0;
    pen.orientation = 45;

    event.touches.push_back(pen);
    EvDevEvents events = event.touches[0].toEvDevEvents(1024, 768, &registry);
    // Expected events.
    EvDevEvents expected_events = {{EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_MAX},
                                   {EV_KEY, BTN_TOOL_RUBBER, 1},
                                   {EV_KEY, BTN_STYLUS, 1},
                                   {EV_ABS, ABS_MT_TRACKING_ID, 0},
                                   {EV_ABS, ABS_MT_SLOT, 0},
                                   {EV_ABS, ABS_MT_ORIENTATION, 45},
                                   {EV_ABS, ABS_MT_PRESSURE, 50},
                                   {EV_ABS, ABS_MT_POSITION_X, 31},
                                   {EV_ABS, ABS_MT_POSITION_Y, 85}};

    EXPECT_EQ(events.size(), expected_events.size());
    EXPECT_THAT(events, Eq(expected_events));
}

TEST(PenTouchEventTest, SendEvents) {
    // Create a mock display.
    auto evloop = TestEventLoop::create();
    MockDisplay display(evloop.get(), 0, 1024, 768);

    // Create a PointerEventDispatcher.
    PointerEventDispatcher dispatcher;

    // Create a PenTouchEvent.
    PenTouchEvent pen_event;
    Pen pen;
    pen.button_pressed = true;
    pen.rubber_pointer = true;
    pen.x = 100;
    pen.y = 200;
    pen.identifier = 0x0a;
    pen.pressure = 50;
    pen.touch_major = 10;
    pen.touch_minor = 5;
    pen.orientation = 45;
    pen_event.touches.push_back(pen);

    // Expect calls to sendEvDevEvent on the mock display.
    {
        InSequence seq;
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_MAX));
        EXPECT_CALL(display, sendEvDevEvent(EV_KEY, BTN_TOOL_RUBBER, 1));
        EXPECT_CALL(display, sendEvDevEvent(EV_KEY, BTN_STYLUS, 1));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TRACKING_ID, 0));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_SLOT, 0));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOUCH_MAJOR, 10));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOUCH_MINOR, 5));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_ORIENTATION, 45));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_PRESSURE, 50));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_POSITION_X, _));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_POSITION_Y, _));
        EXPECT_CALL(display, sendEvDevEvent(EV_SYN, SYN_REPORT, 0));
    }

    // Call sendEvents.
    dispatcher.sendEvents(display, pen_event);
}

struct PointerEventDispatcherUnderTest : public PointerEventDispatcher {
    SlotRegistry* registry() { return &mRegistry; }
};

TEST(PenTouchEventTest, SendEventsWithOldSlots) {
    // Create a mock display, note we are using a nicemock
    // as we will make a series of calls to our mock that we
    // do not care about.
    auto evloop = TestEventLoop::create();
    ::testing::NiceMock<MockDisplay> display(evloop.get(), 0, 1024, 768);

    // Create a PointerEventDispatcher.
    PointerEventDispatcherUnderTest dispatcher;
    dispatcher.registry()->setSlotExpiration(absl::Milliseconds(1));

    // Create a PenTouchEvent.
    PenTouchEvent pen_event;
    Pen pen;
    pen.button_pressed = true;
    pen.rubber_pointer = true;
    pen.x = 100;
    pen.y = 200;
    pen.identifier = 0x0a;
    pen.pressure = 50;
    pen.touch_major = 10;
    pen.touch_minor = 5;
    pen.orientation = 45;
    pen_event.touches.push_back(pen);

    // Call sendEvents.
    dispatcher.sendEvents(display, pen_event);
    std::this_thread::sleep_for(10ms);
    // Expect calls to sendEvDevEvent on the mock display.
    {
        InSequence seq;
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_SLOT, _));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TRACKING_ID, MTS_POINTER_UP));
        EXPECT_CALL(display, sendEvDevEvent(EV_SYN, SYN_REPORT, 0));
    }
    // Call sendEvents with no events, which will cleanup the old ones.
    PenTouchEvent pen_empty;
    dispatcher.sendEvents(display, pen_empty);
}

TEST(TouchEvDevTest, toEvDevEventsPress) {
    SlotRegistry registry;
    Touch touch;
    touch.x = 100;
    touch.y = 200;
    touch.identifier = 1;
    touch.pressure = 50;
    touch.touch_major = 10;
    touch.touch_minor = 5;
    touch.orientation = 45;

    EvDevEvents events = touch.toEvDevEvents(1024, 768, &registry);

    // Expected events for a touch press.
    EvDevEvents expected_events = {
        {EV_ABS, ABS_MT_TRACKING_ID, 0},   {EV_ABS, ABS_MT_SLOT, 0},
        {EV_ABS, ABS_MT_TOUCH_MAJOR, 10},  {EV_ABS, ABS_MT_TOUCH_MINOR, 5},
        {EV_ABS, ABS_MT_ORIENTATION, 45},  {EV_ABS, ABS_MT_PRESSURE, 50},
        {EV_ABS, ABS_MT_POSITION_X, 3199}, {EV_ABS, ABS_MT_POSITION_Y, 8533}};

    EXPECT_EQ(events.size(), expected_events.size());
    EXPECT_THAT(events, Eq(expected_events));
    EXPECT_TRUE(registry.isSlotRegistered(0));
    EXPECT_TRUE(registry.isIdentifierRegistered(1));
}

TEST(TouchEvDevTest, toEvDevEventsRelease) {
    SlotRegistry registry;
    // First create a press event
    Touch touch;
    touch.x = 100;
    touch.y = 200;
    touch.identifier = 1;
    touch.pressure = 50;
    touch.touch_major = 10;
    touch.touch_minor = 5;
    touch.orientation = 45;
    EvDevEvents events = touch.toEvDevEvents(1024, 768, &registry);

    // Now release it
    touch.pressure = 0;
    touch.touch_major = 0;
    touch.touch_minor = 0;
    touch.orientation = 0;

    EvDevEvents release_events = touch.toEvDevEvents(1024, 768, &registry);

    EvDevEvents expected_release_events = {{EV_ABS, ABS_MT_SLOT, 0},
                                           {EV_ABS, ABS_MT_PRESSURE, 0},
                                           {EV_ABS, ABS_MT_POSITION_X, 0xc7f},
                                           {EV_ABS, ABS_MT_POSITION_Y, 0x2155},
                                           {EV_ABS, ABS_MT_TRACKING_ID, MTS_POINTER_UP},
                                           {EV_ABS, ABS_MT_ORIENTATION, 0}};
    EXPECT_EQ(release_events.size(), expected_release_events.size());
    EXPECT_THAT(release_events, Eq(expected_release_events));
    EXPECT_FALSE(registry.isSlotRegistered(0));
    EXPECT_FALSE(registry.isIdentifierRegistered(1));
}

TEST(TouchEvDevTest, toEvDevEventsNoPressure) {
    SlotRegistry registry;
    Touch touch;
    touch.x = 100;
    touch.y = 200;
    touch.identifier = 1;
    touch.pressure = 0;
    touch.touch_major = 10;
    touch.touch_minor = 5;
    touch.orientation = 45;

    EvDevEvents events = touch.toEvDevEvents(1024, 768, &registry);
    EXPECT_EQ(events.size(), 0);
    EXPECT_FALSE(registry.isIdentifierRegistered(1));
}

TEST(TouchEvDevTest, NoReleaseTwice) {
    SlotRegistry registry;
    // First create a press event
    Touch touch;
    touch.x = 100;
    touch.y = 200;
    touch.identifier = 1;
    touch.pressure = 50;
    touch.touch_major = 10;
    touch.touch_minor = 5;
    touch.orientation = 45;
    EvDevEvents events = touch.toEvDevEvents(1024, 768, &registry);

    // Now release it
    touch.pressure = 0;
    EvDevEvents release_events = touch.toEvDevEvents(1024, 768, &registry);

    // Now release again
    EvDevEvents release_again_events = touch.toEvDevEvents(1024, 768, &registry);
    EXPECT_EQ(release_again_events.size(), 0);
    EXPECT_FALSE(registry.isSlotRegistered(0));
    EXPECT_FALSE(registry.isIdentifierRegistered(1));
}

TEST(MultiTouchEventTest, SendEventsPress) {
    // Create a mock display.
    auto evloop = TestEventLoop::create();
    MockDisplay display(evloop.get(), 0, 1024, 768);

    // Create a PointerEventDispatcher.
    PointerEventDispatcher dispatcher;

    // Create a MultiTouchEvent.
    MultiTouchEvent touch_event;
    Touch touch;
    touch.x = 100;
    touch.y = 200;
    touch.identifier = 1;
    touch.pressure = 50;
    touch.touch_major = 10;
    touch.touch_minor = 5;
    touch.orientation = 45;
    touch_event.touches.push_back(touch);

    // Expect calls to sendEvDevEvent on the mock display.
    {
        InSequence seq;
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TRACKING_ID, 0));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_SLOT, 0));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOUCH_MAJOR, 10));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOUCH_MINOR, 5));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_ORIENTATION, 45));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_PRESSURE, 50));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_POSITION_X, _));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_POSITION_Y, _));
        EXPECT_CALL(display, sendEvDevEvent(EV_SYN, SYN_REPORT, 0));
    }

    // Call sendEvents.
    dispatcher.sendEvents(display, touch_event);
}

TEST(MultiTouchEventTest, SendEventsWithOldSlots) {
    // Create a mock display, note we are using a nicemock
    // as we will make a series of calls to our mock that we
    // do not care about.
    auto evloop = TestEventLoop::create();
    ::testing::NiceMock<MockDisplay> display(evloop.get(), 0, 1024, 768);

    // Create a PointerEventDispatcher.
    PointerEventDispatcherUnderTest dispatcher;
    dispatcher.registry()->setSlotExpiration(absl::Milliseconds(1));

    // Create a MultiTouchEvent.
    MultiTouchEvent touch_event;
    Touch touch;
    touch.x = 100;
    touch.y = 200;
    touch.identifier = 1;
    touch.pressure = 50;
    touch.touch_major = 10;
    touch.touch_minor = 5;
    touch.orientation = 45;
    touch_event.touches.push_back(touch);

    // Call sendEvents.
    dispatcher.sendEvents(display, touch_event);
    std::this_thread::sleep_for(10ms);

    // Expect calls to sendEvDevEvent on the mock display.
    {
        InSequence seq;
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_SLOT, _));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TRACKING_ID, MTS_POINTER_UP));
        EXPECT_CALL(display, sendEvDevEvent(EV_SYN, SYN_REPORT, 0));
    }

    // Call sendEvents with no events, which will cleanup the old ones.
    MultiTouchEvent touch_empty;
    dispatcher.sendEvents(display, touch_empty);
}

TEST(MultiTouchEventTest, SendEventsMultipleTouch) {
    // Create a mock display.
    auto evloop = TestEventLoop::create();
    MockDisplay display(evloop.get(), 0, 1024, 768);

    // Create a PointerEventDispatcher.
    PointerEventDispatcher dispatcher;

    // Create a MultiTouchEvent.
    MultiTouchEvent touch_event;
    Touch touch1;
    touch1.x = 100;
    touch1.y = 200;
    touch1.identifier = 1;
    touch1.pressure = 50;
    touch1.touch_major = 10;
    touch1.touch_minor = 5;
    touch1.orientation = 45;
    touch_event.touches.push_back(touch1);

    Touch touch2;
    touch2.x = 300;
    touch2.y = 400;
    touch2.identifier = 2;
    touch2.pressure = 50;
    touch2.touch_major = 12;
    touch2.touch_minor = 6;
    touch2.orientation = 90;
    touch_event.touches.push_back(touch2);

    // Expect calls to sendEvDevEvent on the mock display.
    {
        InSequence seq;
        // First touch events.
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TRACKING_ID, 0));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_SLOT, 0));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOUCH_MAJOR, 10));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOUCH_MINOR, 5));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_ORIENTATION, 45));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_PRESSURE, 50));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_POSITION_X, _));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_POSITION_Y, _));
        // Second touch events.
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TRACKING_ID, 1));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_SLOT, 1));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOUCH_MAJOR, 12));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_TOUCH_MINOR, 6));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_ORIENTATION, 90));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_PRESSURE, 50));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_POSITION_X, _));
        EXPECT_CALL(display, sendEvDevEvent(EV_ABS, ABS_MT_POSITION_Y, _));
        // syn
        EXPECT_CALL(display, sendEvDevEvent(EV_SYN, SYN_REPORT, 0));
    }

    // Call sendEvents.
    dispatcher.sendEvents(display, touch_event);
}

}  // namespace internal
}  // namespace control
}  // namespace emulation
}  // namespace android