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

#include "goldfish/display/test/fake_pixman_display.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/display_frames.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"
#include "goldfish/display/test/display_test_peer.h"
#include "goldfish/display/test/mock_display.h"
#include "goldfish/display/test/pixman_image_generator.h"

using goldfish::async::EventLoop;
using goldfish::display::Dimensions;
using goldfish::display::DisplayTestPeer;
using goldfish::display::FrameEvent;
using goldfish::display::FrameId;
using goldfish::display::FramePtr;
using goldfish::display::ImageRotation;
using goldfish::display::PixelFormat;
using goldfish::display::PixmanImagePtr;
using goldfish::display::test::ActiveFakePixmanDisplay;
using goldfish::display::test::FakePixmanDisplay;

class FakePixmanDisplayTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop_ = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create());
    }

    void TearDown() override { loop_.reset(); }

    // Returns once every task posted to the loop before this call has run.
    void SyncLoop() {
        absl::Notification ran;
        ASSERT_TRUE(loop_->Post([&ran] { ran.Notify(); }).ok());
        ASSERT_TRUE(ran.WaitForNotificationWithTimeout(absl::Seconds(10)));
    }

    std::unique_ptr<EventLoop> loop_;
};

TEST_F(FakePixmanDisplayTest, ActiveFakePixmanDisplayTest) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);

    // Start the generator
    display->Start();

    // Wait for a few frames
    display->WaitForFramesWithTimeout(5, absl::Milliseconds(1000));

    // Stop the generator
    display->Stop();

    // Check if the display has been updated
    ::pixman_image_t* current_image = display->Image().get();
    ASSERT_NE(current_image, nullptr);
    ASSERT_EQ(pixman_image_get_width(current_image), width);
    ASSERT_EQ(pixman_image_get_height(current_image), height);

    // We should have received at least a few frames.
    ASSERT_GT(display->CurrentFrameId(), 2U);
}

TEST_F(FakePixmanDisplayTest, GetScreenshotRGBA8888) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);

    // Get the screenshot
    size_t c_pixels = static_cast<size_t>(width) * height * 4;
    std::vector<uint8_t> pixels(c_pixels);
    auto result = display->GetPixels(PixelFormat::kRgba8888, width, height,
                                     ImageRotation::kRotation0, pixels.data(), &c_pixels);
    ASSERT_TRUE(result.ok());

    // Check if the screenshot has the correct size
    ASSERT_EQ(c_pixels, static_cast<size_t>(width) * height * 4);

    // Check if the screenshot has the correct data (at least one pixel)
    const auto* pixel_data = reinterpret_cast<const uint32_t*>(pixels.data());
    ASSERT_NE(pixel_data[0], 0);
}

TEST_F(FakePixmanDisplayTest, GetScreenshotRGB888) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);

    // Get the screenshot
    size_t c_pixels = static_cast<size_t>(width) * height * 3;
    std::vector<uint8_t> pixels(c_pixels);
    auto result = display->GetPixels(PixelFormat::kRgb888, width, height, ImageRotation::kRotation0,
                                     pixels.data(), &c_pixels);
    ASSERT_TRUE(result.ok());

    // Check if the screenshot has the correct size
    ASSERT_EQ(c_pixels, static_cast<size_t>(width) * height * 3);

    // Check if the screenshot has the correct data (at least one pixel)
    const auto* pixel_data = reinterpret_cast<const uint8_t*>(pixels.data());
    // RGB, so we expect at least a R/G/B pixel.
    ASSERT_NE(pixel_data[0] | pixel_data[1] | pixel_data[2], 0);
}

TEST_F(FakePixmanDisplayTest, GetScreenshotBufferTooSmall) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);

    size_t c_pixels = 10;
    std::vector<uint8_t> pixels(c_pixels);
    auto result = display->GetPixels(PixelFormat::kRgba8888, width, height,
                                     ImageRotation::kRotation0, pixels.data(), &c_pixels);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);

    ASSERT_GT(c_pixels, 10);
}

TEST_F(FakePixmanDisplayTest, GetScreenshotResizeBuffer) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);

    // First call with a too small buffer
    size_t c_pixels = 10;
    std::vector<uint8_t> pixels(c_pixels);
    auto result = display->GetPixels(PixelFormat::kRgba8888, width, height,
                                     ImageRotation::kRotation0, pixels.data(), &c_pixels);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
    ASSERT_GT(c_pixels, 10);

    // Resize the buffer to the correct size
    pixels.resize(c_pixels);

    // Second call with the resized buffer
    result = display->GetPixels(PixelFormat::kRgba8888, width, height, ImageRotation::kRotation0,
                                pixels.data(), &c_pixels);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(c_pixels, static_cast<size_t>(width) * height * 4);

    // Check if the screenshot has the correct data (at least one blue pixel)
    const auto* pixel_data = reinterpret_cast<const uint32_t*>(pixels.data());
    ASSERT_NE(pixel_data[0] | pixel_data[1] | pixel_data[2] | pixel_data[3], 0);
}

TEST_F(FakePixmanDisplayTest, GetPixelsRotationPortraitSource) {
    const int src_w = 100;
    const int src_h = 200;

    // 1. Create a Portrait source image (Black with one RED pixel at Top-Left 0,0)
    const PixmanImagePtr src_image(
            pixman_image_create_bits(PIXMAN_a8r8g8b8, src_w, src_h, nullptr, 0));
    auto* src_data = pixman_image_get_data(src_image.get());
    memset(src_data, 0, static_cast<size_t>(src_w) * src_h * 4);
    src_data[0] = 0xFFFF0000;  // Red in PIXMAN_a8r8g8b8 (AARRGGBB)

    auto display = std::make_shared<FakePixmanDisplay>(loop_.get(), 1, src_image.get());

    auto verify_pixel = [&](ImageRotation rot, int expected_x, int expected_y, int dest_w,
                            int dest_h) {
        size_t c_pixels = static_cast<size_t>(dest_w) * dest_h * 4;
        std::vector<uint8_t> buffer(c_pixels);
        auto result = display->GetPixels(PixelFormat::kRgba8888, dest_w, dest_h, rot, buffer.data(),
                                         &c_pixels);
        ASSERT_TRUE(result.ok()) << "Rotation " << static_cast<int>(rot) << " failed";

        const auto* pixels = reinterpret_cast<const uint32_t*>(buffer.data());
        const uint32_t color = pixels[(static_cast<size_t>(expected_y) * dest_w) + expected_x];
        // PixelFormat::kRgba8888 writes R,G,B,A in memory (0xFF0000FF on little-endian).
        EXPECT_EQ(color, 0xFF0000FFU)
                << "Portrait Rotation " << static_cast<int>(rot)
                << " failed: Expected red pixel at (" << expected_x << ", " << expected_y << ")";
    };

    // Note: ImageRotation is counter-clockwise.
    // 0°: Red at (0, 0)
    verify_pixel(ImageRotation::kRotation0, 0, 0, src_w, src_h);
    // 90° CCW: Top-Left (0,0) moves to Bottom-Left (0, 99)
    verify_pixel(ImageRotation::kRotation90, 0, 99, src_h, src_w);
    // 180° CCW: Top-Left (0,0) moves to Bottom-Right (99, 199)
    verify_pixel(ImageRotation::kRotation180, 99, 199, src_w, src_h);
    // 270° CCW: Top-Left (0,0) moves to Top-Right (199, 0)
    verify_pixel(ImageRotation::kRotation270, 199, 0, src_h, src_w);
}

TEST_F(FakePixmanDisplayTest, GetPixelsRotationLandscapeSource) {
    const int src_w = 200;
    const int src_h = 100;

    // Create a Landscape source image (Red pixel at Top-Left 0,0)
    const PixmanImagePtr src_image(
            pixman_image_create_bits(PIXMAN_a8r8g8b8, src_w, src_h, nullptr, 0));
    auto* src_data = pixman_image_get_data(src_image.get());
    memset(src_data, 0, static_cast<size_t>(src_w) * src_h * 4);
    src_data[0] = 0xFFFF0000;

    auto display = std::make_shared<FakePixmanDisplay>(loop_.get(), 1, src_image.get());

    auto verify_pixel = [&](ImageRotation rot, int expected_x, int expected_y, int dest_w,
                            int dest_h) {
        size_t c_pixels = static_cast<size_t>(dest_w) * dest_h * 4;
        std::vector<uint8_t> buffer(c_pixels);
        auto result = display->GetPixels(PixelFormat::kRgba8888, dest_w, dest_h, rot, buffer.data(),
                                         &c_pixels);
        ASSERT_TRUE(result.ok());
        const auto* pixels = reinterpret_cast<const uint32_t*>(buffer.data());
        EXPECT_EQ(pixels[(static_cast<size_t>(expected_y) * dest_w) + expected_x], 0xFF0000FFU)
                << "Landscape Rotation " << static_cast<int>(rot) << " failed";
    };

    // 0°: (0,0) -> (0,0)
    verify_pixel(ImageRotation::kRotation0, 0, 0, src_w, src_h);
    // 90° CCW: (0,0) -> (0, 199) of 100x200
    verify_pixel(ImageRotation::kRotation90, 0, 199, src_h, src_w);
    // 180° CCW: (0,0) -> (199, 99) of 200x100
    verify_pixel(ImageRotation::kRotation180, 199, 99, src_w, src_h);
    // 270° CCW: (0,0) -> (99, 0) of 100x200
    verify_pixel(ImageRotation::kRotation270, 99, 0, src_h, src_w);
}

TEST_F(FakePixmanDisplayTest, GetPixelsRotationSquareSource) {
    const int size = 100;

    // Create a Square source image (Red pixel at Top-Left 0,0)
    const PixmanImagePtr src_image(
            pixman_image_create_bits(PIXMAN_a8r8g8b8, size, size, nullptr, 0));
    auto* src_data = pixman_image_get_data(src_image.get());
    memset(src_data, 0, static_cast<size_t>(size) * size * 4);
    src_data[0] = 0xFFFF0000;

    auto display = std::make_shared<FakePixmanDisplay>(loop_.get(), 1, src_image.get());

    auto verify_pixel = [&](ImageRotation rot, int expected_x, int expected_y) {
        size_t c_pixels = static_cast<size_t>(size) * size * 4;
        std::vector<uint8_t> buffer(c_pixels);
        auto result = display->GetPixels(PixelFormat::kRgba8888, size, size, rot, buffer.data(),
                                         &c_pixels);
        ASSERT_TRUE(result.ok());
        const auto* pixels = reinterpret_cast<const uint32_t*>(buffer.data());
        EXPECT_EQ(pixels[(static_cast<size_t>(expected_y) * size) + expected_x], 0xFF0000FFU)
                << "Square Rotation " << static_cast<int>(rot) << " failed";
    };

    verify_pixel(ImageRotation::kRotation0, 0, 0);
    verify_pixel(ImageRotation::kRotation90, 0, 99);
    verify_pixel(ImageRotation::kRotation180, 99, 99);
    verify_pixel(ImageRotation::kRotation270, 99, 0);
}

TEST_F(FakePixmanDisplayTest, InitialImageIsBlue) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);

    // Get the initial image
    ::pixman_image_t* initial_image = display->Image().get();
    ASSERT_NE(initial_image, nullptr);

    // Check the dimensions
    ASSERT_EQ(pixman_image_get_width(initial_image), width);
    ASSERT_EQ(pixman_image_get_height(initial_image), height);

    // Check if all pixels are blue
    const auto* pixels = reinterpret_cast<const uint32_t*>(pixman_image_get_data(initial_image));
    bool all_pixels_blue = true;
    for (int i = 0; i < width * height; ++i) {
        if (pixels[i] != 0xFF0000FF) {  // Blue
            all_pixels_blue = false;
            break;
        }
    }
    ASSERT_TRUE(all_pixels_blue) << "Not all pixels are blue.";
}

TEST_F(FakePixmanDisplayTest, ResizeEmitsFrameWithUpdatedDimensions) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);
    absl::Mutex events_mutex;
    std::vector<FrameEvent> events;

    auto listener = display->AddFrameListener([&](const FrameEvent& event) {
        absl::MutexLock lock(&events_mutex);
        events.push_back(event);
    });

    display->Start();
    display->WaitForFramesWithTimeout(2, absl::Milliseconds(500));
    display->Resize(200, 100);
    display->WaitForFramesWithTimeout(4, absl::Milliseconds(500));
    display->Stop();

    absl::MutexLock lock(&events_mutex);
    ASSERT_GE(events.size(), 2U);
    bool saw_geometry_change = false;
    for (size_t i = 1; i < events.size(); ++i) {
        if (events[i].HasGeometryChangeFrom(events[i - 1])) {
            EXPECT_EQ(events[i - 1].dimensions, (Dimensions{100, 50}));
            EXPECT_EQ(events[i].dimensions, (Dimensions{200, 100}));
            saw_geometry_change = true;
        }
    }
    EXPECT_TRUE(saw_geometry_change);
}

TEST_F(FakePixmanDisplayTest, FrameEventsAreOnTheEventLoop) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;
    std::atomic_int frames = 0;
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);

    auto listener = display->AddFrameListener([&](const FrameEvent& /*event*/) {
        frames++;
        ASSERT_TRUE(loop_->IsOnLoopThread())
                << "Event should have been delivered on the event loop";
    });
    display->Start();
    display->WaitForFramesWithTimeout(2, absl::Milliseconds(500));
    display->Resize(200, 100);
    display->WaitForFramesWithTimeout(4, absl::Milliseconds(500));
    display->Stop();

    EXPECT_GT(frames, 0);
}

// A warm display whose guest stops updating and whose consumers drop their
// frames publishes nothing more: S4 re-captures only a capture that was
// skipped for lack of a free slot, never on every frame release.
TEST_F(FakePixmanDisplayTest, WarmIdleDisplayStopsCapturingOnceFramesAreReleased) {
    const PixmanImagePtr image(pixman_image_create_bits(PIXMAN_a8r8g8b8, 16, 16, nullptr, 0));
    auto display = std::make_shared<FakePixmanDisplay>(loop_.get(), 1, image.get());
    auto listener = display->AddFrameListener([](const FrameEvent& /*event*/) {});
    absl::StatusOr<FramePtr> fresh = display->WaitForFreshFrame(absl::Now() + absl::Seconds(10));
    ASSERT_TRUE(fresh.ok()) << fresh.status();
    SyncLoop();

    // Each request replaces the latest frame, whose slot is released inside
    // PublishFrame.
    const FrameId warm_id = display->CurrentFrameId();
    for (int i = 0; i < 6; ++i) {
        DisplayTestPeer::producer(*display)->RequestFrame();
        SyncLoop();
    }
    ASSERT_EQ(display->CurrentFrameId(), warm_id + 6);

    *fresh = nullptr;  // The consumer drops its frame too.
    SyncLoop();
    absl::SleepFor(absl::Milliseconds(50));
    SyncLoop();
    EXPECT_EQ(display->CurrentFrameId(), warm_id + 6);
}

// DISPLAY_PRODUCER_SIMPLIFICATION.md finding #16: the legacy source requests
// its owed capture inside Resize, which PixmanDisplay calls with its mutex
// held. Here all four slots are leased, so a request is owed, and the new size
// in UpdateSourceImage frees room for it.
TEST_F(FakePixmanDisplayTest, OwedCaptureDuringUpdateSourceImageDoesNotSelfDeadlock) {
    const PixmanImagePtr image(pixman_image_create_bits(PIXMAN_a8r8g8b8, 16, 16, nullptr, 0));
    const PixmanImagePtr resized(pixman_image_create_bits(PIXMAN_a8r8g8b8, 32, 32, nullptr, 0));
    auto display = std::make_shared<FakePixmanDisplay>(loop_.get(), 1, image.get());
    auto held = std::make_shared<std::vector<FramePtr>>();
    // Frame requests only capture for a warm display.
    DisplayTestPeer::producer(*display)->SetDemand(true);
    SyncLoop();
    for (int i = 0; i < 3; ++i) {
        held->push_back(display->AcquireLatestFrame().frame);
        DisplayTestPeer::producer(*display)->RequestFrame();
        SyncLoop();
    }
    const FrameId before = display->CurrentFrameId();
    // Three frames held plus the latest: no free slot.
    DisplayTestPeer::producer(*display)->RequestFrame();
    SyncLoop();
    ASSERT_EQ(display->CurrentFrameId(), before);

    auto done = std::make_shared<absl::Notification>();
    // `resized` stays owned here; the display takes its own reference.
    std::thread updater([display, image = resized.get(), done] {
        display->UpdateSourceImage(image);
        done->Notify();
    });
    if (!done->WaitForNotificationWithTimeout(absl::Seconds(10))) {
        // Leak what the deadlocked thread holds rather than hang the test.
        updater.detach();
        (void)new std::shared_ptr<std::vector<FramePtr>>(std::move(held));
        (void)new PixmanImagePtr(resized);
        FAIL() << "PixmanDisplay self-deadlocked re-capturing inside UpdateSourceImage";
    }
    updater.join();
    SyncLoop();
    EXPECT_GT(display->CurrentFrameId(), before);
}
