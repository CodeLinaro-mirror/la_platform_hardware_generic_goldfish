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
#include "android/emulation/control/event_sender.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/display/test/mock_display.h"

namespace android::emulation::control {

using ::goldfish::async::testing::TestEventLoop;
using ::goldfish::display::DisplayId;
using ::goldfish::display::DisplayPtr;
using ::goldfish::display::IMultiDisplay;
using ::goldfish::display::test::MockDisplay;
using ::testing::_;
using ::testing::Return;

class MockMultiDisplay : public IMultiDisplay {
  public:
    using IMultiDisplay::IMultiDisplay;
    MOCK_METHOD(absl::StatusOr<DisplayPtr>, CreateDisplay,
                (DisplayId, uint32_t, uint32_t, uint32_t, uint32_t), (override));
    MOCK_METHOD(bool, IsEnabled, (), (const, override));
    MOCK_METHOD(absl::StatusOr<DisplayPtr>, GetDisplay, (DisplayId), (const, override));
    MOCK_METHOD(absl::Status, EraseDisplay, (DisplayId), (override));
    MOCK_METHOD(std::vector<DisplayPtr>, Displays, (), (const, override));

    MOCK_METHOD(bool, IsActive, (DisplayId), (const, override));
    MOCK_METHOD(absl::Status, SetActive, (DisplayId, bool), (override));
    MOCK_METHOD(void, SetFolded, (bool), (override));
    MOCK_METHOD(bool, IsFolded, (), (const, override));
    MOCK_METHOD(void, SetDisplayMode, (uint32_t, uint32_t, uint32_t, uint32_t, uint32_t),
                (override));
    MOCK_METHOD(uint32_t, GetDisplayMode, (), (const, override));
    MOCK_METHOD(absl::Status, Save, (goldfish::archive::IWriter&), (const, override));
    MOCK_METHOD(absl::Status, Load, (goldfish::archive::IReader&), (override));
    MOCK_METHOD(void, Reset, (), (override));
};

class InputEventSenderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ev_loop_ = TestEventLoop::Create();
        mock_multidisplay_ = std::make_unique<MockMultiDisplay>(ev_loop_.get());
        sender_ = std::make_unique<InputEventSender>(mock_multidisplay_.get());
        mock_display_ = std::make_shared<MockDisplay>(ev_loop_.get(), 0, 1024, 768);
        ON_CALL(*mock_multidisplay_, IsActive(_)).WillByDefault(Return(true));
    }

    std::unique_ptr<TestEventLoop> ev_loop_;
    std::unique_ptr<MockMultiDisplay> mock_multidisplay_;
    std::unique_ptr<InputEventSender> sender_;
    std::shared_ptr<MockDisplay> mock_display_;
};

TEST_F(InputEventSenderTest, SendAndroidEvent) {
    AndroidEvent event;
    event.set_display(0);
    event.set_type(1);
    event.set_code(2);
    event.set_value(3);

    EXPECT_CALL(*mock_multidisplay_, GetDisplay(0)).WillOnce(Return(mock_display_));
    EXPECT_CALL(*mock_display_, SendEvDevEvent(1, 2, 3));

    EXPECT_TRUE(sender_->Send(event).ok());
}

TEST_F(InputEventSenderTest, SendMouseEvent) {
    MouseEvent event;
    event.set_display(0);
    event.set_x(100);
    event.set_y(200);
    event.set_buttons(1);

    EXPECT_CALL(*mock_multidisplay_, GetDisplay(0)).WillOnce(Return(mock_display_));
    EXPECT_CALL(*mock_display_, SendMouseEvent(100, 200, 1));

    EXPECT_TRUE(sender_->Send(event).ok());
}

TEST_F(InputEventSenderTest, SendWheelEvent) {
    WheelEvent event;
    // Wheel events are currently not supported, should return Ok but log error.
    // Ensure no calls are made to the mock multidisplay or display.
    EXPECT_CALL(*mock_multidisplay_, GetDisplay(_)).Times(0);
    EXPECT_TRUE(InputEventSender::Send(event).ok());
}

TEST_F(InputEventSenderTest, InactiveDisplay) {
    AndroidEvent event;
    event.set_display(0);

    // If display 0 is inactive, GetActiveDisplay will try display 1 if there's a hinge.
    // For this test, let's just make it return an inactive display and no hinge.
    EXPECT_CALL(*mock_multidisplay_, GetDisplay(0)).WillOnce(Return(mock_display_));
    EXPECT_CALL(*mock_multidisplay_, IsActive(0)).WillRepeatedly(Return(false));

    // InputEventSender::Send calls TryLockDisplay which calls GetActiveDisplay.
    // GetActiveDisplay returns UnavailableError if the display is inactive and no redirect happens.
    EXPECT_FALSE(sender_->Send(event).ok());
}

TEST_F(InputEventSenderTest, SendTouchEvent) {
    TouchEvent event;
    event.set_display(0);
    auto* touch = event.add_touches();
    touch->set_x(100);
    touch->set_y(200);
    touch->set_identifier(1);
    touch->set_pressure(50);

    EXPECT_CALL(*mock_multidisplay_, GetDisplay(0)).WillOnce(Return(mock_display_));
    // PointerEventDispatcher will call SendEvDevEvent multiple times.
    // We just verify that it doesn't crash and returns OK.
    EXPECT_CALL(*mock_display_, SendEvDevEvent(_, _, _)).Times(::testing::AtLeast(1));

    EXPECT_TRUE(sender_->Send(event).ok());
}

TEST_F(InputEventSenderTest, SendPenEvent) {
    PenEvent event;
    event.set_display(0);
    auto* pen = event.add_events();
    auto* touch = pen->mutable_location();
    touch->set_x(100);
    touch->set_y(200);
    touch->set_identifier(1);
    touch->set_pressure(50);

    EXPECT_CALL(*mock_multidisplay_, GetDisplay(0)).WillOnce(Return(mock_display_));
    EXPECT_CALL(*mock_display_, SendEvDevEvent(_, _, _)).Times(::testing::AtLeast(1));

    EXPECT_TRUE(sender_->Send(event).ok());
}

TEST_F(InputEventSenderTest, DisplayNotFound) {
    AndroidEvent event;
    event.set_display(1);

    EXPECT_CALL(*mock_multidisplay_, GetDisplay(1))
            .WillOnce(Return(absl::NotFoundError("Display not found")));

    EXPECT_FALSE(sender_->Send(event).ok());
}

}  // namespace android::emulation::control
