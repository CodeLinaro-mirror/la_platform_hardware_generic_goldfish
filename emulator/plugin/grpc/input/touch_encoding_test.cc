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
#include "touch_encoding.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <ostream>
#include <vector>

#include "slot_registry.h"
#include "standard-headers/linux/input-event-codes.h"
#include "standard-headers/linux/input.h"

namespace goldfish::display {

void PrintTo(const EvDevEvent& ev, std::ostream* os) {
    *os << "{type=" << ev.type << ", code=" << ev.code << ", value=" << ev.value << "}";
}

}  // namespace goldfish::display

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::control::EvDevEvent;
using ::android::emulation::control::EvDevEvents;
using ::android::emulation::control::kMtsPointerUp;
using ::android::emulation::control::kMtsPressureMax;
using ::android::emulation::v2::PointerAction;
using ::android::emulation::v2::PointerEvent;
using ::android::emulation::v2::ToolType;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::Not;

constexpr uint32_t kDefaultTouchMajor = 0x500;

PointerEvent::Pointer MakePointer(ToolType tool_type = ToolType::TOOL_TYPE_FINGER) {
    PointerEvent::Pointer pointer;
    pointer.set_x(0.0F);
    pointer.set_y(1.0F);
    pointer.set_tool_type(tool_type);
    return pointer;
}

EvDevEvents DownOrMove(const PointerEvent::Pointer& pointer, PointerAction action, bool new_contact,
                       int slot = 3) {
    EvDevEvents events;
    AppendContactDownOrMove(pointer, action, slot, new_contact, events);
    return events;
}

TEST(TouchEncodingTest, NewContactGetsTrackingIdEqualToSlot) {
    EXPECT_THAT(DownOrMove(MakePointer(), PointerAction::POINTER_ACTION_DOWN, true),
                ElementsAre(EvDevEvent{EV_ABS, ABS_MT_SLOT, 3},
                            EvDevEvent{EV_ABS, ABS_MT_TRACKING_ID, 3},
                            EvDevEvent{EV_ABS, ABS_MT_POSITION_X, 0},
                            EvDevEvent{EV_ABS, ABS_MT_POSITION_Y, 0x7FFF},
                            EvDevEvent{EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER},
                            EvDevEvent{EV_ABS, ABS_MT_PRESSURE, kMtsPressureMax},
                            EvDevEvent{EV_ABS, ABS_MT_TOUCH_MAJOR, kDefaultTouchMajor},
                            EvDevEvent{EV_ABS, ABS_MT_TOUCH_MINOR, kDefaultTouchMajor}));
}

TEST(TouchEncodingTest, ExistingContactOmitsTrackingId) {
    EXPECT_THAT(DownOrMove(MakePointer(), PointerAction::POINTER_ACTION_MOVE, false),
                ElementsAre(EvDevEvent{EV_ABS, ABS_MT_SLOT, 3},
                            EvDevEvent{EV_ABS, ABS_MT_POSITION_X, 0},
                            EvDevEvent{EV_ABS, ABS_MT_POSITION_Y, 0x7FFF},
                            EvDevEvent{EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER},
                            EvDevEvent{EV_ABS, ABS_MT_PRESSURE, kMtsPressureMax},
                            EvDevEvent{EV_ABS, ABS_MT_TOUCH_MAJOR, kDefaultTouchMajor},
                            EvDevEvent{EV_ABS, ABS_MT_TOUCH_MINOR, kDefaultTouchMajor}));
}

TEST(TouchEncodingTest, HoverEncodesZeroPressure) {
    auto pointer = MakePointer();
    pointer.set_pressure(0.5F);
    EXPECT_THAT(DownOrMove(pointer, PointerAction::POINTER_ACTION_HOVER_ENTER, true),
                Contains(EvDevEvent{EV_ABS, ABS_MT_PRESSURE, 0}));
    EXPECT_THAT(DownOrMove(pointer, PointerAction::POINTER_ACTION_HOVER_MOVE, false),
                Contains(EvDevEvent{EV_ABS, ABS_MT_PRESSURE, 0}));
}

TEST(TouchEncodingTest, PressureScalesToDeviceRange) {
    auto pointer = MakePointer();
    pointer.set_pressure(0.5F);
    EXPECT_THAT(DownOrMove(pointer, PointerAction::POINTER_ACTION_MOVE, false),
                Contains(EvDevEvent{EV_ABS, ABS_MT_PRESSURE, kMtsPressureMax / 2}));
}

TEST(TouchEncodingTest, SizeScalesToAbsRange) {
    auto pointer = MakePointer();
    pointer.set_size(0.5F);
    EXPECT_THAT(DownOrMove(pointer, PointerAction::POINTER_ACTION_MOVE, false),
                Contains(EvDevEvent{EV_ABS, ABS_MT_TOUCH_MAJOR, 0x3FFF}));
}

TEST(TouchEncodingTest, OrientationClampsToDegreeBoundsAndOmitsZeroOrNan) {
    auto nan_pointer = MakePointer();
    nan_pointer.set_orientation_radians(std::numeric_limits<float>::quiet_NaN());
    EXPECT_THAT(DownOrMove(nan_pointer, PointerAction::POINTER_ACTION_MOVE, false),
                Not(Contains(Field(&EvDevEvent::code, ABS_MT_ORIENTATION))));

    auto inf_pointer = MakePointer();
    inf_pointer.set_orientation_radians(std::numeric_limits<float>::infinity());
    EXPECT_THAT(DownOrMove(inf_pointer, PointerAction::POINTER_ACTION_MOVE, false),
                Contains(EvDevEvent{EV_ABS, ABS_MT_ORIENTATION, 90}));

    auto neg_large_pointer = MakePointer();
    neg_large_pointer.set_orientation_radians(-1e20F);
    EXPECT_THAT(DownOrMove(neg_large_pointer, PointerAction::POINTER_ACTION_MOVE, false),
                Contains(EvDevEvent{EV_ABS, ABS_MT_ORIENTATION, static_cast<uint32_t>(-90)}));
}

TEST(TouchEncodingTest, TiltedStylusAndEraserEmitTilt) {
    for (ToolType tool_type : {ToolType::TOOL_TYPE_STYLUS, ToolType::TOOL_TYPE_ERASER}) {
        auto pointer = MakePointer(tool_type);
        pointer.set_tilt_radians(0.785398163F);  // 45 degrees, pointing up (orientation 0).
        const auto events = DownOrMove(pointer, PointerAction::POINTER_ACTION_MOVE, false);
        EXPECT_THAT(events, Contains(EvDevEvent{EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PEN}));
        EXPECT_THAT(events, Contains(EvDevEvent{EV_ABS, ABS_TILT_X, 0}));
        EXPECT_THAT(events, Contains(EvDevEvent{EV_ABS, ABS_TILT_Y, static_cast<uint32_t>(-45)}));
    }
}

TEST(TouchEncodingTest, TiltedStylusClampsExtremeTiltValues) {
    auto pointer = MakePointer(ToolType::TOOL_TYPE_STYLUS);
    pointer.set_tilt_radians(1e20F);
    pointer.set_orientation_radians(0.0F);
    const auto events = DownOrMove(pointer, PointerAction::POINTER_ACTION_MOVE, false);
    EXPECT_THAT(events, Contains(EvDevEvent{EV_ABS, ABS_TILT_X, 0}));
    EXPECT_THAT(events, Contains(EvDevEvent{EV_ABS, ABS_TILT_Y, static_cast<uint32_t>(-90)}));
}

TEST(TouchEncodingTest, TiltedFingerOmitsTilt) {
    auto pointer = MakePointer(ToolType::TOOL_TYPE_FINGER);
    pointer.set_tilt_radians(0.785398163F);
    const auto events = DownOrMove(pointer, PointerAction::POINTER_ACTION_MOVE, false);
    EXPECT_THAT(events, Not(Contains(Field(&EvDevEvent::code, ABS_TILT_X))));
    EXPECT_THAT(events, Not(Contains(Field(&EvDevEvent::code, ABS_TILT_Y))));
}

TEST(TouchEncodingTest, ContactUpSelectsSlotAndClearsTrackingId) {
    EvDevEvents events;
    AppendContactUp(7, events);
    EXPECT_THAT(events, ElementsAre(EvDevEvent{EV_ABS, ABS_MT_SLOT, 7},
                                    EvDevEvent{EV_ABS, ABS_MT_TRACKING_ID, kMtsPointerUp}));
}

}  // namespace
}  // namespace goldfish::grpc::v2
