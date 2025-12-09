// PixmanImageGeneratorTest.cpp
#include "emulator/libs/display/include/goldfish/display/test/pixman_image_generator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "absl/time/time.h"

#include "emulator/libs/display/include/goldfish/display/test/fake_pixman_display.h"
#include "emulator/libs/display/include/goldfish/display/test/mock_display.h"

using android::base::eventing::EventListener;

using namespace goldfish::display;
using namespace goldfish::display::test;

class PixmanImageGeneratorTest : public ::testing::Test {
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

class ImageListener : public EventListener<PixmanImagePtr> {
  public:
    void eventArrived(const PixmanImagePtr& img) override { images.push_back(img); }
    std::vector<PixmanImagePtr> images;
};

TEST_F(PixmanImageGeneratorTest, ImageGenerationSequence) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    generator.addListener(listener);

    generator.start();
    generator.waitForFramesWithTimeout(5, absl::Milliseconds(1000));
    generator.stop();

    ASSERT_GE(listener->images.size(), 3);  // Should have at least 3 images

    for (size_t i = 0; i < listener->images.size(); ++i) {
        uint32_t expectedColor;
        switch (i % 3) {
        case 0:
            expectedColor = 0xFFFF0000;  // Red
            break;
        case 1:
            expectedColor = 0xFF00FF00;  // Green
            break;
        case 2:
            expectedColor = 0xFF0000FF;  // Blue
            break;
        }

        uint32_t* pixels = (uint32_t*)pixman_image_get_data(listener->images[i].get());
        bool allPixelsMatch = true;
        for (int j = 0; j < width * height; ++j) {
            if (pixels[j] != expectedColor) {
                allPixelsMatch = false;
                break;
            }
        }
        ASSERT_TRUE(allPixelsMatch) << "Image " << i << " has incorrect color.";
        ASSERT_EQ(pixman_image_get_width(listener->images[i].get()), width);
        ASSERT_EQ(pixman_image_get_height(listener->images[i].get()), height);
    }
}

TEST_F(PixmanImageGeneratorTest, FpsAccuracy) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    generator.addListener(listener);

    generator.start();
    auto start = std::chrono::steady_clock::now();
    generator.waitForFramesWithTimeout(10, absl::Milliseconds(2000));
    auto end = std::chrono::steady_clock::now();
    generator.stop();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    double actualFps = (double)listener->images.size() / (elapsed.count() / 1000.0);

    ASSERT_NEAR(actualFps, fps, 5.0);  // Allow some tolerance on our slow build bots.
}

TEST_F(PixmanImageGeneratorTest, StartStop) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    ;
    generator.addListener(listener);

    generator.start();
    generator.waitForFramesWithTimeout(1, absl::Milliseconds(200));
    generator.stop();
    size_t imageCountAfterStop = listener->images.size();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(listener->images.size(), imageCountAfterStop);  // No new images after stop

    generator.start();
    generator.waitForFramesWithTimeout(imageCountAfterStop + 1, absl::Milliseconds(200));
    generator.stop();
    ASSERT_GT(listener->images.size(), imageCountAfterStop);  // New images after restart
}

TEST_F(PixmanImageGeneratorTest, EventFiring) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    generator.addListener(listener);

    generator.start();
    generator.waitForFramesWithTimeout(2, absl::Milliseconds(500));
    generator.stop();

    ASSERT_GT(listener->images.size(), 0);  // At least one event should have been fired
}

TEST_F(PixmanImageGeneratorTest, Resize) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    generator.addListener(listener);

    generator.start();
    generator.waitForFramesWithTimeout(2, absl::Milliseconds(500));
    generator.resize(200, 100);
    generator.waitForFramesWithTimeout(4, absl::Milliseconds(500));
    generator.stop();

    ASSERT_GT(listener->images.size(), 0);
    auto lastImage = listener->images.back();
    ASSERT_EQ(pixman_image_get_width(lastImage.get()), 200);
    ASSERT_EQ(pixman_image_get_height(lastImage.get()), 100);
}

TEST_F(PixmanImageGeneratorTest, WaitForFrames) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    generator.start();
    EXPECT_TRUE(generator.waitForFramesWithTimeout(5, absl::Milliseconds(1000)));
    EXPECT_GE(generator.frameCount(), 5);
    EXPECT_FALSE(generator.waitForFramesWithTimeout(100, absl::Milliseconds(100)));
    generator.stop();
}