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

#include "goldfish/display/display.h"

#include <thread>

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

using goldfish::async::EventLoop;
using goldfish::display::Dimensions;
using goldfish::display::FrameInfo;
using goldfish::display::IDisplay;
using goldfish::display::ImageRotation;
using goldfish::display::MultiTouchType;
using goldfish::display::PixelFormat;

// Test Display implementation for unit testing.
class TestDisplay : public IDisplay {
  public:
    TestDisplay(EventLoop* loop, uint8_t id, uint32_t width, uint32_t height)
            : IDisplay(loop, id, width, height) {}

    absl::StatusOr<FrameInfo> GetPixels(PixelFormat /*fmt*/, int /*width*/, int /*height*/,
                                        ImageRotation /*rotation*/, uint8_t* /*pixel*/,
                                        size_t* /*c_pixels*/) const override {
        return absl::InvalidArgumentError("This display does not exist.");
    }

    void SendMultiTouchEvent(uint8_t /*slot*/, int /*x*/, int /*y*/,
                             MultiTouchType /*type*/) override {}
    void SendMouseEvent(int /*x*/, int /*y*/, int /*button_mask*/) override {}
    void SendEvDevEvent(uint16_t /*type*/, uint16_t /*code*/, uint32_t /*value*/) override {}
    void SetSeq(uint64_t seq) {
        const absl::MutexLock lock(seq_access_);
        seq_.sequence_number = seq;
    }

    void UpdateDimensions(uint32_t w, uint32_t h) { SetDimensions(w, h); }

    void Incoming() { FrameReceived(); }
};

class DisplayTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop_ = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create());
    }

    void TearDown() override { loop_.reset(); }

    std::unique_ptr<EventLoop> loop_;
};

TEST_F(DisplayTest, WaitForFrameTimeout) {
    const TestDisplay display(loop_.get(), 0, 100, 100);
    const uint64_t initial_seq = display.Seq().sequence_number;

    // Test timeout.
    auto timeout = absl::Milliseconds(10);
    EXPECT_FALSE(display.WaitForFrame(timeout, initial_seq));
}

TEST_F(DisplayTest, WaitForFrameSuccess) {
    TestDisplay display(loop_.get(), 0, 100, 100);
    const uint64_t initial_seq = display.Seq().sequence_number;

    std::thread frame_updater([&display, initial_seq]() {
        absl::SleepFor(absl::Milliseconds(5));  // Simulate frame update delay
        display.SetSeq(initial_seq + 1);
        display.Incoming();  // Notify waiting threads
    });

    auto timeout = absl::Seconds(1);
    EXPECT_TRUE(display.WaitForFrame(timeout, initial_seq));
    frame_updater.join();
}

TEST_F(DisplayTest, WaitForNextFrameSuccess) {
    TestDisplay display(loop_.get(), 0, 100, 100);

    std::thread frame_updater([&display]() {
        absl::SleepFor(absl::Milliseconds(5));  // Simulate frame update delay
        display.Incoming();                     // Notify waiting threads
    });

    auto timeout = absl::Seconds(1);
    EXPECT_TRUE(display.WaitForNextFrame(timeout));
    frame_updater.join();
}

TEST_F(DisplayTest, WaitForNextFrameTimeout) {
    const TestDisplay display(loop_.get(), 0, 100, 100);

    auto timeout = absl::Milliseconds(10);
    EXPECT_FALSE(display.WaitForNextFrame(timeout));
}

TEST_F(DisplayTest, GetDimensions) {
    const uint32_t width = 800;
    const uint32_t height = 600;
    const TestDisplay display(loop_.get(), 0, width, height);
    const Dimensions dims = display.GetDimensions();
    EXPECT_EQ(dims.width, width);
    EXPECT_EQ(dims.height, height);
}

TEST_F(DisplayTest, SetDimensions) {
    TestDisplay display(loop_.get(), 0, 100, 100);
    display.UpdateDimensions(1920, 1080);
    const Dimensions dims = display.GetDimensions();
    EXPECT_EQ(dims.width, 1920);
    EXPECT_EQ(dims.height, 1080);
}

TEST_F(DisplayTest, ThreadSafeDimensions) {
    TestDisplay display(loop_.get(), 0, 100, 101);
    std::atomic<bool> running{true};

    std::thread writer([&]() {
        uint32_t i = 0;
        while (running) {
            display.UpdateDimensions(i, i + 1);
            i++;
        }
    });

    for (int i = 0; i < 10000; ++i) {
        const Dimensions dims = display.GetDimensions();
        EXPECT_EQ(dims.height, dims.width + 1);
    }

    running = false;
    writer.join();
}

// Ensure TestDisplay can track lifecycle hooks for testing.
class LifecycleMockDisplay : public TestDisplay {
  public:
    LifecycleMockDisplay(EventLoop* loop, uint8_t id, uint32_t width, uint32_t height)
            : TestDisplay(loop, id, width, height) {}

    void OnListenerAdded() override { added_count++; }
    void OnListenerRemoved() override { removed_count++; }

    int added_count = 0;
    int removed_count = 0;
};

TEST_F(DisplayTest, AddFrameListener_ReceivesFramesAsync) {
    LifecycleMockDisplay display(loop_.get(), 0, 100, 100);

    absl::Notification frame_arrived;
    auto listener = display.AddFrameListener([&](const FrameInfo&) {
        if (!frame_arrived.HasBeenNotified()) {
            frame_arrived.Notify();
        }
    });

    // Simulate QEMU pushing a frame
    display.Incoming();

    // The notification should arrive.
    EXPECT_TRUE(frame_arrived.WaitForNotificationWithTimeout(absl::Seconds(1)));
}

TEST_F(DisplayTest, AddFrameListener_FiresImmediatelyWhenAlreadyActive) {
    LifecycleMockDisplay display(loop_.get(), 0, 100, 100);

    // Initial state: No subscribers, no frames.
    // Attach listener 1: First subscriber (0 -> 1). Should NOT fire immediately.
    absl::Notification frame1_arrived;
    auto listener1 = display.AddFrameListener([&](const FrameInfo&) {
        if (!frame1_arrived.HasBeenNotified()) {
            frame1_arrived.Notify();
        }
    });

    // Simulate QEMU pushing a frame to the active display.
    display.Incoming();

    // Wait for the first frame to arrive on the event loop.
    ASSERT_TRUE(frame1_arrived.WaitForNotificationWithTimeout(absl::Seconds(1)));

    // Attach listener 2 while listener 1 is active and a valid frame exists.
    // It MUST fire immediately and synchronously during AddFrameListener.
    int listener2_frame_count = 0;
    uint64_t listener2_received_seq = 0;
    auto listener2 = display.AddFrameListener([&](const FrameInfo& info) {
        listener2_frame_count++;
        listener2_received_seq = info.sequence_number;
    });

    // Verify listener 2 received the current frame synchronously before AddFrameListener returned.
    EXPECT_EQ(1, listener2_frame_count);
    EXPECT_EQ(1, listener2_received_seq);
}

TEST_F(DisplayTest, AddFrameListener_TriggersHooks) {
    LifecycleMockDisplay display(loop_.get(), 0, 100, 100);

    EXPECT_EQ(0, display.added_count);
    EXPECT_EQ(0, display.removed_count);

    {
        auto listener = display.AddFrameListener([](const FrameInfo&) {});

        EXPECT_EQ(1, display.added_count);
        EXPECT_EQ(0, display.removed_count);
    }

    EXPECT_EQ(1, display.added_count);
    EXPECT_EQ(1, display.removed_count);
}

TEST_F(DisplayTest, DisplayEventProxy_ForwardsEventsAndManagesLifetime) {
    auto display = std::make_shared<LifecycleMockDisplay>(loop_.get(), 0, 100, 100);
    EXPECT_EQ(0, display->added_count);

    {
        auto proxy = std::make_unique<goldfish::display::DisplayEventProxy>(display);
        EXPECT_EQ(1, display->added_count);
        EXPECT_EQ(0, display->removed_count);

        absl::Notification frame_arrived;
        auto callback = android::base::eventing::MakeScopedCallback(*proxy, [&](const FrameInfo&) {
            if (!frame_arrived.HasBeenNotified()) {
                frame_arrived.Notify();
            }
        });

        display->Incoming();
        EXPECT_TRUE(frame_arrived.WaitForNotificationWithTimeout(absl::Seconds(1)));
    }

    // After proxy destruction, the listener should be removed.
    EXPECT_EQ(1, display->removed_count);
}

TEST_F(DisplayTest, DisplayEventProxy_HandlesDisplayDestroyedFirst) {
    auto display = std::make_shared<LifecycleMockDisplay>(loop_.get(), 0, 100, 100);
    std::weak_ptr<LifecycleMockDisplay> weak_display = display;
    auto proxy = std::make_unique<goldfish::display::DisplayEventProxy>(display);

    // Proxy should hold a weak reference and not extend display lifetime
    EXPECT_EQ(1, weak_display.use_count());

    // Destroy the display before the proxy
    display.reset();
    EXPECT_TRUE(weak_display.expired());

    // Destroying proxy should not crash even if the display is gone
    proxy.reset();
}

TEST_F(DisplayTest, DisplayEventProxy_HandlesNullDisplay) {
    auto proxy = std::make_unique<goldfish::display::DisplayEventProxy>(std::weak_ptr<IDisplay>{});
    EXPECT_NO_FATAL_FAILURE(proxy.reset());
}
