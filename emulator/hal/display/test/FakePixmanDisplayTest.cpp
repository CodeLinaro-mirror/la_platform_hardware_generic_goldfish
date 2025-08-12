// PixmanImageGeneratorTest.cpp
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "FakePixmanDisplay.h"
#include "MockDisplay.h"
#include "PixmanImageGenerator.h"
#include "aemu/base/events/EventSupport.h"

extern "C" {
#include "pixman.h"
}

namespace android::goldfish {
using android::base::EventListener;

// Test case for default constructor (nullptr)
TEST(PixmanImagePtr, DefaultConstructor) {
    PixmanImagePtr ptr;
    ASSERT_EQ(ptr.get(), nullptr);
}

::pixman_image_t* generateImage(int w, int h) {
    uint32_t* pixels = new uint32_t[w * h];
    uint32_t colorValue = 0xFFFF0000;  // RED
    for (int i = 0; i < w * h; ++i) {
        pixels[i] = colorValue;
    }
    return pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h, pixels, h * sizeof(uint32_t));
}

// Test case for constructor with a valid pixman_image_t*, use asan to validate!
TEST(PixmanImagePtr, ValidImageConstructor) {
    ::pixman_image_t* image = generateImage(32, 20);
    ASSERT_NE(image, nullptr);

    {
        // Create a PixmanImagePtr
        PixmanImagePtr ptr(image);
        ASSERT_EQ(ptr.get(), image);
    }
    ASSERT_TRUE(pixman_image_unref(image));
}

// Test case for destructor (unref) use asan to validate refcounts!
TEST(PixmanImagePtr, Destructor) {
    ::pixman_image_t* image = generateImage(32, 20);
    ASSERT_NE(image, nullptr);

    // Create a PixmanImagePtr
    {
        PixmanImagePtr ptr(image);
        ASSERT_EQ(ptr.get(), image);
    }
    pixman_image_unref(image);
}

TEST(PixmanImagePtr, ArrowOperator) {
    // Test case for -> operator
    ::pixman_image_t* image = generateImage(32, 20);
    ASSERT_NE(image, nullptr);

    // Create a PixmanImagePtr
    PixmanImagePtr ptr(image);
    ASSERT_EQ(ptr.get(), image);

    // Clean up (implicitly done by ptr destructor)
    pixman_image_unref(image);
}

TEST(PixmanImagePtr, GetMethod) {
    ::pixman_image_t* image = generateImage(32, 20);
    ASSERT_NE(image, nullptr);

    // Create a PixmanImagePtr
    PixmanImagePtr ptr(image);
    ASSERT_EQ(ptr.get(), image);

    // Clean up (implicitly done by ptr destructor)
    pixman_image_unref(image);
}

// Test case for multiple PixmanImagePtrs referencing the same image
TEST(PixmanImagePtr, MultipleReferences) {
    ::pixman_image_t* image = generateImage(32, 20);
    ASSERT_NE(image, nullptr);

    // Create a PixmanImagePtr
    PixmanImagePtr ptr1(image);
    PixmanImagePtr ptr2(image);
    {
        PixmanImagePtr ptr3(image);
    }

    // Clean up (implicitly done by ptr destructors)
    pixman_image_unref(image);
}

TEST(PixmanImagePtr, MoveConstructor) {
    ::pixman_image_t* image = generateImage(32, 20);
    ASSERT_NE(image, nullptr);

    // Create a PixmanImagePtr
    PixmanImagePtr ptr1(image);

    // Move construct
    PixmanImagePtr ptr2(std::move(ptr1));

    // Verify ptr1 is null
    ASSERT_EQ(ptr1.get(), nullptr);
    ASSERT_EQ(ptr2.get(), image);
    pixman_image_unref(image);
}

TEST(PixmanImagePtr, MoveAssignmentOperator) {
    ::pixman_image_t* image1 = generateImage(32, 20);
    ASSERT_NE(image1, nullptr);
    ::pixman_image_t* image2 = generateImage(32, 20);
    ASSERT_NE(image2, nullptr);

    // Create PixmanImagePtr
    PixmanImagePtr ptr1(image1);
    PixmanImagePtr ptr2(image2);

    // Move assign
    ptr2 = std::move(ptr1);

    // Verify ptr1 is null and ptr2 holds the image
    ASSERT_EQ(ptr1.get(), nullptr);
    ASSERT_EQ(ptr2.get(), image1);

    pixman_image_unref(image1);
    pixman_image_unref(image2);
}

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
    display.waitForFramesWithTimeout(5, absl::Milliseconds(1000));

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

class TestListener : public EventListener<ResizeEvent> {
  public:
    void eventArrived(ResizeEvent event) override { events.push_back(event); }
    std::vector<ResizeEvent> events;
};

TEST(FakePixmanDisplayTest, ResizeEvent) {
    int fps = 10;
    int width = 100;
    int height = 50;
    int id = 0;

    // Create an ActiveFakePixmanDisplay
    ActiveFakePixmanDisplay display = ActiveFakePixmanDisplay::create(id, fps, width, height);
    TestListener listener;
    display.EventChangeSupport<ResizeEvent>::addListener(&listener);

    // Start the generator
    display.start();
    display.waitForFramesWithTimeout(2, absl::Milliseconds(500));
    display.resize(200, 100);
    display.waitForFramesWithTimeout(4, absl::Milliseconds(500));
    display.stop();

    ASSERT_EQ(listener.events.size(), 1);
    EXPECT_EQ(listener.events[0].previousWidth, 100);
    EXPECT_EQ(listener.events[0].previousHeight, 50);
    EXPECT_EQ(listener.events[0].width, 200);
    EXPECT_EQ(listener.events[0].height, 100);
}

}  // namespace android::goldfish