// PixmanImageGeneratorTest.cpp
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "FakePixmanDisplay.h"
#include "MockDisplay.h"
#include "PixmanImageGenerator.h"
#include "android/emulation/control/utils/EventSupport.h"

extern "C" {
#include "pixman.h"
}

namespace android::goldfish {
using android::emulation::control::EventListener;

TEST(FakePixmanDisplayTest, ActiveFakePixmanDisplayTest) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    ActiveFakePixmanDisplay display = ActiveFakePixmanDisplay::create(id, fps, width, height);

    // Start the generator
    display.start();

    // Wait for a few frames
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Stop the generator
    display.stop();

    // Check if the display has been updated
    ::pixman_image_t* currentImage = display.image();
    ASSERT_NE(currentImage, nullptr);
    ASSERT_EQ(pixman_image_get_width(currentImage), width);
    ASSERT_EQ(pixman_image_get_height(currentImage), height);

    // We should have received at least a few frames.
    ASSERT_GT(display.seq().sequenceNumber, 2);
}

TEST(FakePixmanDisplayTest, GetScreenshotRGBA8888) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    ActiveFakePixmanDisplay display = ActiveFakePixmanDisplay::create(id, fps, width, height);

    // Get the screenshot
    size_t cPixels = width * height * 4;
    std::vector<uint8_t> pixels(cPixels);
    auto result =
            display.getPixels(PixelFormat::RGBA8888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_TRUE(result.ok());

    // Check if the screenshot has the correct size
    ASSERT_EQ(cPixels, width * height * 4);

    // Check if the screenshot has the correct data (at least one pixel)
    uint32_t* pixelData = reinterpret_cast<uint32_t*>(pixels.data());
    ASSERT_NE(pixelData[0], 0);
}

TEST(FakePixmanDisplayTest, GetScreenshotRGB888) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    ActiveFakePixmanDisplay display = ActiveFakePixmanDisplay::create(id, fps, width, height);

    // Get the screenshot
    size_t cPixels = width * height * 3;
    std::vector<uint8_t> pixels(cPixels);
    auto result = display.getPixels(PixelFormat::RGB888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_TRUE(result.ok());

    // Check if the screenshot has the correct size
    ASSERT_EQ(cPixels, width * height * 3);

    // Check if the screenshot has the correct data (at least one pixel)
    uint8_t* pixelData = reinterpret_cast<uint8_t*>(pixels.data());
    // RGB, so we expect at least a R/G/B pixel.
    ASSERT_NE(pixelData[0] | pixelData[1] | pixelData[2], 0);
}

TEST(FakePixmanDisplayTest, GetScreenshotBufferTooSmall) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    ActiveFakePixmanDisplay display = ActiveFakePixmanDisplay::create(id, fps, width, height);

    // Get the screenshot with a too small buffer
    size_t cPixels = 10;
    std::vector<uint8_t> pixels(cPixels);
    auto result =
            display.getPixels(PixelFormat::RGBA8888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);

    ASSERT_GT(cPixels, 10);
}

TEST(FakePixmanDisplayTest, GetScreenshotResizeBuffer) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    ActiveFakePixmanDisplay display = ActiveFakePixmanDisplay::create(id, fps, width, height);

    // First call with a too small buffer
    size_t cPixels = 10;
    std::vector<uint8_t> pixels(cPixels);
    auto result =
            display.getPixels(PixelFormat::RGBA8888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
    ASSERT_GT(cPixels, 10);

    // Resize the buffer to the correct size
    pixels.resize(cPixels);

    // Second call with the resized buffer
    result = display.getPixels(PixelFormat::RGBA8888, width, height, 0, pixels.data(), &cPixels);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(cPixels, width * height * 4);

    // Check if the screenshot has the correct data (at least one blue pixel)
    uint32_t* pixelData = reinterpret_cast<uint32_t*>(pixels.data());
    ASSERT_NE(pixelData[0] | pixelData[1] | pixelData[2] | pixelData[3], 0);
}

TEST(FakePixmanDisplayTest, InitialImageIsBlue) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    ActiveFakePixmanDisplay display = ActiveFakePixmanDisplay::create(id, fps, width, height);

    // Get the initial image
    ::pixman_image_t* initialImage = display.image();
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

}  // namespace android::goldfish