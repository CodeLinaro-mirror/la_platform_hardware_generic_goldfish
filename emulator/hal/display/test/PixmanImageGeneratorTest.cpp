// PixmanImageGeneratorTest.cpp
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "absl/time/time.h"

#include "FakePixmanDisplay.h"
#include "MockDisplay.h"
#include "PixmanImageGenerator.h"
#include "aemu/base/events/EventSupport.h"

extern "C" {
#include "pixman.h"
}

namespace android::goldfish {
using android::base::EventListener;

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
    generator.waitForFramesWithTimeout(5, absl::Milliseconds(1000));
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
    generator.waitForFramesWithTimeout(10, absl::Milliseconds(2000));
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
    generator.waitForFramesWithTimeout(1, absl::Milliseconds(200));
    generator.stop();
    size_t imageCountAfterStop = listener.images.size();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(listener.images.size(), imageCountAfterStop);  // No new images after stop

    generator.start();
    generator.waitForFramesWithTimeout(imageCountAfterStop + 1, absl::Milliseconds(200));
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
    generator.waitForFramesWithTimeout(2, absl::Milliseconds(500));
    generator.stop();

    ASSERT_GT(listener.images.size(), 0);  // At least one event should have been fired
}

TEST_F(PixmanImageGeneratorTest, Resize) {
    int fps = 10;
    int width = 100;
    int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    ImageListener listener;
    generator.addListener(&listener);

    generator.start();
    generator.waitForFramesWithTimeout(2, absl::Milliseconds(500));
    generator.resize(200, 100);
    generator.waitForFramesWithTimeout(4, absl::Milliseconds(500));
    generator.stop();

    ASSERT_GT(listener.images.size(), 0);
    auto lastImage = listener.images.back();
    ASSERT_EQ(pixman_image_get_width(lastImage), 200);
    ASSERT_EQ(pixman_image_get_height(lastImage), 100);
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

}  // namespace android::goldfish
