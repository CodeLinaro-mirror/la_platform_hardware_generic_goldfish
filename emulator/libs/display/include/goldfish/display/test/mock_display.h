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

#pragma once

#include <gmock/gmock.h>

#include "goldfish/display/display.h"

namespace goldfish::display::test {

using ::testing::InSequence;
using ::testing::Mock;

class MockDisplay : public IDisplay {
  public:
    MockDisplay(EventLoop* loop, uint8_t id, uint32_t width, uint32_t height)
            : IDisplay(loop, id, width, height) {}
    MOCK_METHOD(absl::StatusOr<FrameInfo>, getPixels,
                (PixelFormat, int, int, ImageRotation, uint8_t*, size_t*), (const, override));
    MOCK_METHOD(void, sendMultiTouchEvent, (uint8_t slot, int x, int y, MultiTouchType type),
                (override));
    MOCK_METHOD(void, sendMouseEvent, (int x, int y, int button_mask), (override));
    MOCK_METHOD(void, sendEvDevEvent, (uint16_t type, uint16_t code, uint32_t value), (override));
};

}  // namespace goldfish::display::test
