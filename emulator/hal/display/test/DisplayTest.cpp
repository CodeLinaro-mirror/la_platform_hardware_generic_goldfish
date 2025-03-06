// Copyright 2025 The Android Open Source Project
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

#include <thread>

#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include "android/goldfish/display/Display.h"

namespace android::goldfish {
namespace {

// Test Display implementation for unit testing.
class TestDisplay : public IDisplay {
  public:
    TestDisplay(uint8_t id, uint32_t width, uint32_t height) : IDisplay(id, width, height) {}

    absl::StatusOr<FrameInfo> getPixels(PixelFormat fmt, int width, int height, int rotationDeg,
                                        uint8_t* pixel, size_t* cPixels) const override {
        return absl::InvalidArgumentError("This display does not exist.");
    }

    void sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) override {}
    void sendMouseEvent(int x, int y, int button_mask) override {}
    void sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) override {}
    void setSeq(uint64_t seq) {
        absl::MutexLock lock(&mSeqAccess);
        mSeq.sequenceNumber = seq;
    }

    void incoming() { frameReceived(); }
};

TEST(DisplayTest, WaitForFrameTimeout) {
    TestDisplay display(0, 100, 100);
    uint64_t initialSeq = display.seq().sequenceNumber;

    // Test timeout.
    auto timeout = absl::Milliseconds(10);
    EXPECT_FALSE(display.waitForFrame(timeout, initialSeq));
}

TEST(DisplayTest, WaitForFrameSuccess) {
    TestDisplay display(0, 100, 100);
    uint64_t initialSeq = display.seq().sequenceNumber;

    std::thread frameUpdater([&display, initialSeq]() {
        absl::SleepFor(absl::Milliseconds(5));  // Simulate frame update delay
        display.setSeq(initialSeq + 1);
        display.incoming();  // Notify waiting threads
    });

    auto timeout = absl::Milliseconds(50);
    EXPECT_TRUE(display.waitForFrame(timeout, initialSeq));
    frameUpdater.join();
}

TEST(DisplayTest, WaitForNextFrameSuccess) {
    TestDisplay display(0, 100, 100);

    std::thread frameUpdater([&display]() {
        absl::SleepFor(absl::Milliseconds(5));  // Simulate frame update delay
        display.incoming();                     // Notify waiting threads
    });

    auto timeout = absl::Milliseconds(50);
    EXPECT_TRUE(display.waitForNextFrame(timeout));
    frameUpdater.join();
}

TEST(DisplayTest, WaitForNextFrameTimeout) {
    TestDisplay display(0, 100, 100);

    auto timeout = absl::Milliseconds(10);
    EXPECT_FALSE(display.waitForNextFrame(timeout));
}

}  // namespace
}  // namespace android::goldfish
