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

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/test/mock_display.h"
#include "goldfish/display/test/pixman_image_generator.h"

using android::base::eventing::EventListener;
using goldfish::async::EventLoop;
using goldfish::display::FrameInfo;
using goldfish::display::FrameInfoCallbackSource;
using goldfish::display::ImageRotation;
using goldfish::display::PixelFormat;
using goldfish::display::PixmanImagePtr;
using goldfish::display::ResizeEvent;
using goldfish::display::ResizeEventCallbackSource;
using goldfish::display::test::ActiveFakePixmanDisplay;
using goldfish::display::test::FakePixmanDisplay;

class FakePixmanDisplayTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop_ = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create());
    }

    void TearDown() override { loop_.reset(); }

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
    ASSERT_GT(display->Seq().sequence_number, 2);
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

TEST_F(FakePixmanDisplayTest, DISABLED_GetPixelsRotationPortraitSource) {
    const int src_w = 100;
    const int src_h = 200;

    // 1. Create a Portrait source image (Black with one RED pixel at Top-Left 0,0)
    const PixmanImagePtr src_image(
            pixman_image_create_bits(PIXMAN_a8r8g8b8, src_w, src_h, nullptr, 0));
    auto* src_data = pixman_image_get_data(src_image.get());
    memset(src_data, 0, static_cast<size_t>(src_w) * src_h * 4);
    src_data[0] = 0xFFFF0000;  // Red (AARRGGBB)

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
        EXPECT_EQ(color, 0xFFFF0000)
                << "Portrait Rotation " << static_cast<int>(rot)
                << " failed: Expected red pixel at (" << expected_x << ", " << expected_y << ")";
    };

    // Note: Pixman rotation is counter-clockwise.
    // 0°: Red at (0, 0)
    verify_pixel(ImageRotation::kRotation0, 0, 0, src_w, src_h);
    // 90° CCW: Top-Left (0,0) moves to Bottom-Left (0, 99)
    verify_pixel(ImageRotation::kRotation90, 0, 99, src_h, src_w);
    // 180° CCW: Top-Left (0,0) moves to Bottom-Right (99, 199)
    verify_pixel(ImageRotation::kRotation180, 99, 199, src_w, src_h);
    // 270° CCW: Top-Left (0,0) moves to Top-Right (199, 0)
    verify_pixel(ImageRotation::kRotation270, 199, 0, src_h, src_w);
}

TEST_F(FakePixmanDisplayTest, DISABLED_GetPixelsRotationLandscapeSource) {
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
        EXPECT_EQ(pixels[(static_cast<size_t>(expected_y) * dest_w) + expected_x], 0xFFFF0000)
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

TEST_F(FakePixmanDisplayTest, DISABLED_GetPixelsRotationSquareSource) {
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
        EXPECT_EQ(pixels[(static_cast<size_t>(expected_y) * size) + expected_x], 0xFFFF0000)
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

class TestListener : public EventListener<ResizeEvent> {
  public:
    void EventArrived(const ResizeEvent& event) override {
        const absl::MutexLock lock(events_mutex);
        events.push_back(event);
    }
    absl::Mutex events_mutex;
    std::vector<ResizeEvent> events;
};

TEST_F(FakePixmanDisplayTest, ResizeEvent) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);
    auto listener = std::make_shared<TestListener>();
    display->ResizeEventCallbackSource::AddListener(listener);

    // Start the generator
    display->Start();
    display->WaitForFramesWithTimeout(2, absl::Milliseconds(500));
    display->Resize(200, 100);
    display->WaitForFramesWithTimeout(4, absl::Milliseconds(500));
    display->Stop();

    const absl::MutexLock lock(listener->events_mutex);
    ASSERT_EQ(listener->events.size(), 1);
    EXPECT_EQ(listener->events[0].previous_width, 100);
    EXPECT_EQ(listener->events[0].previous_height, 50);
    EXPECT_EQ(listener->events[0].width, 200);
    EXPECT_EQ(listener->events[0].height, 100);
}

TEST_F(FakePixmanDisplayTest, ResizeEventsAreOnTheEventLoop) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);
    auto listener = std::make_shared<TestListener>();

    auto* callback_source = static_cast<ResizeEventCallbackSource*>(display.get());
    auto callback = android::base::eventing::MakeScopedCallback(
            *callback_source, [&](const ResizeEvent& /*event*/) {
                ASSERT_TRUE(loop_->IsOnLoopThread())
                        << "Event should have been delivered on the event loop";
            });
    // Start the generator
    display->Start();
    display->WaitForFramesWithTimeout(2, absl::Milliseconds(500));
    display->Resize(200, 100);
    display->WaitForFramesWithTimeout(4, absl::Milliseconds(500));
    display->Stop();
}

TEST_F(FakePixmanDisplayTest, FrameInfoEventsAreOnTheEventLoop) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    const int id = 0;
    std::atomic_int frames = 0;
    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), id, fps, width, height);
    auto listener = std::make_shared<TestListener>();

    // Cast the source so our scopedCallback doesn't get confused (display has multiple event
    // sources)
    auto* callback_source = static_cast<FrameInfoCallbackSource*>(display.get());
    auto callback = android::base::eventing::MakeScopedCallback(
            *callback_source, [&](const FrameInfo& /*event*/) {
                frames++;
                ASSERT_TRUE(loop_->IsOnLoopThread())
                        << "Event should have been delivered on the event loop";
            });
    // Start the generator
    display->Start();
    display->WaitForFramesWithTimeout(2, absl::Milliseconds(500));
    display->Stop();

    // We delivered some frames to our callback, where we verified that it is on the event loop
    EXPECT_GT(frames, 0);
}
