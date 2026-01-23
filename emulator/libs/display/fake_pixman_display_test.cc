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

#include "emulator/libs/display/include/goldfish/display/test/fake_pixman_display.h"

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "emulator/libs/display/include/goldfish/display/test/mock_display.h"
#include "emulator/libs/display/include/goldfish/display/test/pixman_image_generator.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

using android::base::eventing::EventListener;
using goldfish::async::EventLoop;

using namespace goldfish::display;
using namespace goldfish::display::test;

class FakePixmanDisplayTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mLoop = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create());
    }

    void TearDown() override { mLoop.reset(); }

    std::unique_ptr<EventLoop> mLoop;
};

TEST_F(FakePixmanDisplayTest, ActiveFakePixmanDisplayTest) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);

    // Start the generator
    display->start();

    // Wait for a few frames
    display->waitForFramesWithTimeout(5, absl::Milliseconds(1000));

    // Stop the generator
    display->stop();

    // Check if the display has been updated
    ::pixman_image_t* currentImage = display->image().get();
    ASSERT_NE(currentImage, nullptr);
    ASSERT_EQ(pixman_image_get_width(currentImage), width);
    ASSERT_EQ(pixman_image_get_height(currentImage), height);

    // We should have received at least a few frames.
    ASSERT_GT(display->seq().sequenceNumber, 2);
}

TEST_F(FakePixmanDisplayTest, GetScreenshotRGBA8888) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);

    // Get the screenshot
    size_t cPixels = width * height * 4;
    std::vector<uint8_t> pixels(cPixels);
    auto result =
            display->getPixels(PixelFormat::RGBA8888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_TRUE(result.ok());

    // Check if the screenshot has the correct size
    ASSERT_EQ(cPixels, width * height * 4);

    // Check if the screenshot has the correct data (at least one pixel)
    uint32_t* pixelData = reinterpret_cast<uint32_t*>(pixels.data());
    ASSERT_NE(pixelData[0], 0);
}

TEST_F(FakePixmanDisplayTest, GetScreenshotRGB888) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);

    // Get the screenshot
    size_t cPixels = width * height * 3;
    std::vector<uint8_t> pixels(cPixels);
    auto result =
            display->getPixels(PixelFormat::RGB888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_TRUE(result.ok());

    // Check if the screenshot has the correct size
    ASSERT_EQ(cPixels, width * height * 3);

    // Check if the screenshot has the correct data (at least one pixel)
    uint8_t* pixelData = reinterpret_cast<uint8_t*>(pixels.data());
    // RGB, so we expect at least a R/G/B pixel.
    ASSERT_NE(pixelData[0] | pixelData[1] | pixelData[2], 0);
}

TEST_F(FakePixmanDisplayTest, GetScreenshotBufferTooSmall) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);

    // Get the screenshot with a too small buffer
    size_t cPixels = 10;
    std::vector<uint8_t> pixels(cPixels);
    auto result =
            display->getPixels(PixelFormat::RGBA8888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);

    ASSERT_GT(cPixels, 10);
}

TEST_F(FakePixmanDisplayTest, GetScreenshotResizeBuffer) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);

    // First call with a too small buffer
    size_t cPixels = 10;
    std::vector<uint8_t> pixels(cPixels);
    auto result =
            display->getPixels(PixelFormat::RGBA8888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
    ASSERT_GT(cPixels, 10);

    // Resize the buffer to the correct size
    pixels.resize(cPixels);

    // Second call with the resized buffer
    result = display->getPixels(PixelFormat::RGBA8888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(cPixels, width * height * 4);

    // Check if the screenshot has the correct data (at least one blue pixel)
    uint32_t* pixelData = reinterpret_cast<uint32_t*>(pixels.data());
    ASSERT_NE(pixelData[0] | pixelData[1] | pixelData[2] | pixelData[3], 0);
}

TEST_F(FakePixmanDisplayTest, InitialImageIsBlue) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);

    // Get the initial image
    ::pixman_image_t* initialImage = display->image().get();
    ASSERT_NE(initialImage, nullptr);

    // Check the dimensions
    ASSERT_EQ(pixman_image_get_width(initialImage), width);
    ASSERT_EQ(pixman_image_get_height(initialImage), height);

    // Check if all pixels are blue
    uint32_t* pixels = (uint32_t*)pixman_image_get_data(initialImage);
    bool allPixelsBlue = true;
    for (int i = 0; i < width * height; ++i) {
        if (pixels[i] != 0xFF0000FF) {  // Blue
            allPixelsBlue = false;
            break;
        }
    }
    ASSERT_TRUE(allPixelsBlue) << "Not all pixels are blue.";
}

class TestListener : public EventListener<ResizeEvent> {
  public:
    void EventArrived(const ResizeEvent& event) override { events.push_back(event); }
    std::vector<ResizeEvent> events;
};

TEST_F(FakePixmanDisplayTest, ResizeEvent) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);
    auto listener = std::make_shared<TestListener>();
    display->ResizeEventCallbackSource::AddListener(listener);

    // Start the generator
    display->start();
    display->waitForFramesWithTimeout(2, absl::Milliseconds(500));
    display->resize(200, 100);
    display->waitForFramesWithTimeout(4, absl::Milliseconds(500));
    display->stop();

    ASSERT_EQ(listener->events.size(), 1);
    EXPECT_EQ(listener->events[0].previousWidth, 100);
    EXPECT_EQ(listener->events[0].previousHeight, 50);
    EXPECT_EQ(listener->events[0].width, 200);
    EXPECT_EQ(listener->events[0].height, 100);
}

TEST_F(FakePixmanDisplayTest, ResizeEventsAreOnTheEventLoop) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);
    auto listener = std::make_shared<TestListener>();

    auto callbackSource = static_cast<ResizeEventCallbackSource*>(display.get());
    auto callback = android::base::eventing::MakeScopedCallback(
            *callbackSource, [&](const ResizeEvent& event) {
                ASSERT_TRUE(mLoop->IsOnLoopThread())
                        << "Event should have been delivered on the event loop";
            });
    // Start the generator
    display->start();
    display->waitForFramesWithTimeout(2, absl::Milliseconds(500));
    display->resize(200, 100);
    display->waitForFramesWithTimeout(4, absl::Milliseconds(500));
    display->stop();
}

TEST_F(FakePixmanDisplayTest, FrameInfoEventsAreOnTheEventLoop) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;
    std::atomic_int frames = 0;
    // Create an ActiveFakePixmanDisplay
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), id, fps, width, height);
    auto listener = std::make_shared<TestListener>();

    // Cast the source so our scopedCallback doesn't get confused (display has multiple event
    // sources)
    auto callbackSource = static_cast<FrameInfoCallbackSource*>(display.get());
    auto callback = android::base::eventing::MakeScopedCallback(
            *callbackSource, [&](const FrameInfo& event) {
                frames++;
                ASSERT_TRUE(mLoop->IsOnLoopThread())
                        << "Event should have been delivered on the event loop";
            });
    // Start the generator
    display->start();
    display->waitForFramesWithTimeout(2, absl::Milliseconds(500));
    display->stop();

    // We delivered some frames to our callback, where we verified that it is on the event loop
    EXPECT_GT(frames, 0);
}