// PixmanImageGeneratorTest.cpp
#include "goldfish/display/test/pixman_image_generator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "absl/time/time.h"

#include "goldfish/display/test/fake_pixman_display.h"
#include "goldfish/display/test/mock_display.h"

using android::base::eventing::EventListener;

using goldfish::display::PixmanImagePtr;
using goldfish::display::test::PixmanImageGenerator;

class PixmanImageGeneratorTest : public ::testing::Test {
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

class ImageListener : public EventListener<PixmanImagePtr> {
  public:
    void EventArrived(const PixmanImagePtr& img) override { images.push_back(img); }
    std::vector<PixmanImagePtr> images;
};

TEST_F(PixmanImageGeneratorTest, ImageGenerationSequence) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    generator.AddListener(listener);

    generator.Start();
    generator.WaitForFramesWithTimeout(5, absl::Milliseconds(1000));
    generator.Stop();

    ASSERT_GE(listener->images.size(), 3);  // Should have at least 3 images

    for (size_t i = 0; i < listener->images.size(); ++i) {
        uint32_t expected_color = 0;
        switch (i % 3) {
        case 0:
            expected_color = 0xFFFF0000;  // kRed
            break;
        case 1:
            expected_color = 0xFF00FF00;  // kGreen
            break;
        case 2:
            expected_color = 0xFF0000FF;  // kBlue
            break;
        default:
            break;
        }

        auto* pixels = pixman_image_get_data(listener->images[i].get());
        bool all_pixels_match = true;
        for (int j = 0; j < width * height; ++j) {
            if (pixels[j] != expected_color) {
                all_pixels_match = false;
                break;
            }
        }
        ASSERT_TRUE(all_pixels_match) << "Image " << i << " has incorrect color.";
        ASSERT_EQ(pixman_image_get_width(listener->images[i].get()), width);
        ASSERT_EQ(pixman_image_get_height(listener->images[i].get()), height);
    }
}

TEST_F(PixmanImageGeneratorTest, FpsAccuracy) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    generator.AddListener(listener);

    generator.Start();
    auto start = std::chrono::steady_clock::now();
    generator.WaitForFramesWithTimeout(10, absl::Milliseconds(2000));
    auto end = std::chrono::steady_clock::now();
    generator.Stop();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    const double actual_fps = static_cast<double>(listener->images.size()) /
                              (static_cast<double>(elapsed.count()) / 1000.0);

    ASSERT_NEAR(actual_fps, fps, 5.0);  // Allow some tolerance on our slow build bots.
}

TEST_F(PixmanImageGeneratorTest, StartStop) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    ;
    generator.AddListener(listener);

    generator.Start();
    generator.WaitForFramesWithTimeout(1, absl::Milliseconds(200));
    generator.Stop();
    const size_t image_count_after_stop = listener->images.size();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(listener->images.size(), image_count_after_stop);  // No new images after stop

    generator.Start();
    generator.WaitForFramesWithTimeout(static_cast<int>(image_count_after_stop + 1),
                                       absl::Milliseconds(200));
    generator.Stop();
    ASSERT_GT(listener->images.size(), image_count_after_stop);  // New images after restart
}

TEST_F(PixmanImageGeneratorTest, EventFiring) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    generator.AddListener(listener);

    generator.Start();
    generator.WaitForFramesWithTimeout(2, absl::Milliseconds(500));
    generator.Stop();

    ASSERT_GT(listener->images.size(), 0);  // At least one event should have been fired
}

TEST_F(PixmanImageGeneratorTest, Resize) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    auto listener = std::make_shared<ImageListener>();
    generator.AddListener(listener);

    generator.Start();
    generator.WaitForFramesWithTimeout(2, absl::Milliseconds(500));
    generator.Resize(200, 100);
    generator.WaitForFramesWithTimeout(4, absl::Milliseconds(500));
    generator.Stop();

    ASSERT_GT(listener->images.size(), 0);
    auto last_image = listener->images.back();
    ASSERT_EQ(pixman_image_get_width(last_image.get()), 200);
    ASSERT_EQ(pixman_image_get_height(last_image.get()), 100);
}

TEST_F(PixmanImageGeneratorTest, WaitForFrames) {
    const int fps = 10;
    const int width = 100;
    const int height = 50;
    PixmanImageGenerator generator(fps, width, height);
    generator.Start();
    EXPECT_TRUE(generator.WaitForFramesWithTimeout(5, absl::Milliseconds(1000)));
    EXPECT_GE(generator.FrameCount(), 5);
    EXPECT_FALSE(generator.WaitForFramesWithTimeout(100, absl::Milliseconds(100)));
    generator.Stop();
}
