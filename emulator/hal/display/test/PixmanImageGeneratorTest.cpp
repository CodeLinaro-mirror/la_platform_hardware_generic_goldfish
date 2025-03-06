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

class PixmanImageGeneratorTest : public ::testing::Test {
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

class ImageListener : public EventListener<::pixman_image_t*> {
  public:
    ~ImageListener() {
        for (auto image : images) {
            pixman_image_unref(image);
        }
    }
    void eventArrived(::pixman_image_t* img) override { images.push_back(pixman_image_ref(img)); }
    std::vector<::pixman_image_t*> images;
};

TEST_F(PixmanImageGeneratorTest, ImageGenerationSequence) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    ImageListener listener;
    generator.addListener(&listener);

    generator.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Wait for a few frames
    generator.stop();

    ASSERT_GE(listener.images.size(), 3);  // Should have at least 3 images

    for (size_t i = 0; i < listener.images.size(); ++i) {
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

        uint32_t* pixels = (uint32_t*)pixman_image_get_data(listener.images[i]);
        bool allPixelsMatch = true;
        for (int j = 0; j < width * height; ++j) {
            if (pixels[j] != expectedColor) {
                allPixelsMatch = false;
                break;
            }
        }
        ASSERT_TRUE(allPixelsMatch) << "Image " << i << " has incorrect color.";
        ASSERT_EQ(pixman_image_get_width(listener.images[i]), width);
        ASSERT_EQ(pixman_image_get_height(listener.images[i]), height);
        pixman_image_unref(listener.images[i]);
    }
}

TEST_F(PixmanImageGeneratorTest, FpsAccuracy) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    ImageListener listener;
    generator.addListener(&listener);

    generator.start();
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Wait for 1 second
    auto end = std::chrono::steady_clock::now();
    generator.stop();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    double actualFps = (double)listener.images.size() / (elapsed.count() / 1000.0);

    ASSERT_NEAR(actualFps, fps, 5.0);  // Allow some tolerance on our slow build bots.
}

TEST_F(PixmanImageGeneratorTest, StartStop) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    ImageListener listener;
    ;
    generator.addListener(&listener);

    generator.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    generator.stop();
    size_t imageCountAfterStop = listener.images.size();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(listener.images.size(), imageCountAfterStop);  // No new images after stop

    generator.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    generator.stop();
    ASSERT_GT(listener.images.size(), imageCountAfterStop);  // New images after restart
}

TEST_F(PixmanImageGeneratorTest, EventFiring) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    ImageListener listener;
    generator.addListener(&listener);

    generator.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    generator.stop();

    ASSERT_GT(listener.images.size(), 0);  // At least one event should have been fired
}

}  // namespace android::goldfish
