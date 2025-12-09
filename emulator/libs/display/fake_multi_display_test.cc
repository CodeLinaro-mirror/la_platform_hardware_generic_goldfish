#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

// #include "goldfish/display/QemuMultidisplay/multi_display.h"

// #include ".h"
// #include "goldfish/display/pixman_display.h"

// #include "goldfish/display/pixman_image_ptr.h"
#include "emulator/libs/display/include/goldfish/display/test/fake_multi_display.h"
#include "emulator/libs/display/include/goldfish/display/test/fake_pixman_display.h"
#include "emulator/libs/display/include/goldfish/display/test/image_generation_strategy.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

using android::base::eventing::EventListener;

using goldfish::async::CallbackEventSource;
using goldfish::async::EventLoop;

using namespace goldfish::display;
using namespace goldfish::display::test;

class FakeMultiDisplayTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mLoop = ::goldfish::async::ThreadedEventLoop::create(
                ::goldfish::async::LibuvEventLoop::create());

        // Clear all displays except the default one before each test
        mFakeMultiDisplay = std::make_unique<FakeMultiDisplay>(mLoop.get());
        IMultiDisplay::injectSingleton(mFakeMultiDisplay.get());
    }
    void TearDown() override { mLoop.reset(); }

    std::unique_ptr<FakeMultiDisplay> mFakeMultiDisplay;
    std::unique_ptr<EventLoop> mLoop;
};

class DisplayEventListener : public EventListener<DisplayEvent> {
  public:
    void eventArrived(const DisplayEvent& event) override {
        std::unique_lock<std::mutex> lock(mMutex);
        events.push_back(event);
        mCv.notify_one();
    }

    bool waitForEvent(size_t eventCount,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCv.wait_for(lock, timeout,
                            [this, eventCount] { return events.size() >= eventCount; });
    }

    std::vector<DisplayEvent> events;

  private:
    std::mutex mMutex;
    std::condition_variable mCv;
};

TEST_F(FakeMultiDisplayTest, CreateAndGetDisplay) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();

    // Create a new display
    auto result = multiDisplay->createDisplay(1, 800, 600);
    ASSERT_TRUE(result.ok());
    DisplayPtr display = result.value();

    // Get the display by ID
    auto getResult = multiDisplay->getDisplay(1);
    ASSERT_TRUE(getResult.ok());
    DisplayPtr retrievedDisplay = getResult.value();

    // Check if the retrieved display is the same as the created one
    ASSERT_EQ(display.lock().get(), retrievedDisplay.lock().get());
}

TEST_F(FakeMultiDisplayTest, CreateDisplayAlreadyExists) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();

    // Create a new display
    auto result = multiDisplay->createDisplay(2, 800, 600);
    ASSERT_TRUE(result.ok());

    // Try to create a display with the same ID
    auto result2 = multiDisplay->createDisplay(2, 1024, 768);
    ASSERT_FALSE(result2.ok());
    ASSERT_EQ(result2.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(FakeMultiDisplayTest, GetDisplayNotFound) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();

    // Try to get a non-existent display
    auto result = multiDisplay->getDisplay(99);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(FakeMultiDisplayTest, EraseDisplay) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();

    // Create a new display
    auto result = multiDisplay->createDisplay(3, 800, 600);
    ASSERT_TRUE(result.ok());

    // Erase the display
    auto eraseResult = multiDisplay->eraseDisplay(3);
    ASSERT_TRUE(eraseResult.ok());

    // Try to get the erased display
    auto getResult = multiDisplay->getDisplay(3);
    ASSERT_FALSE(getResult.ok());
    ASSERT_EQ(getResult.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(FakeMultiDisplayTest, EraseDefaultDisplay) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();

    // Try to erase the default display
    auto result = multiDisplay->eraseDisplay(0);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(FakeMultiDisplayTest, Displays) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();

    // Create a few displays
    auto result1 = multiDisplay->createDisplay(4, 800, 600);
    ASSERT_TRUE(result1.ok());
    auto result2 = multiDisplay->createDisplay(5, 1024, 768);
    ASSERT_TRUE(result2.ok());

    // Get all displays
    std::vector<DisplayPtr> displays = multiDisplay->displays();

    // Check if the number of displays is correct (including the default display)
    ASSERT_EQ(displays.size(), 3);

    // Check if the default display is present
    bool defaultDisplayFound = false;
    for (const auto& display : displays) {
        if (display.lock()->id() == 0) {
            defaultDisplayFound = true;
            break;
        }
    }
    ASSERT_TRUE(defaultDisplayFound);
}

TEST_F(FakeMultiDisplayTest, DefaultDisplay) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();

    // Get the default display
    DisplayPtr defaultDisplay = multiDisplay->defaultDisplay().value();

    // Check if the default display has the correct ID
    ASSERT_EQ(defaultDisplay.lock()->id(), 0);
}

TEST_F(FakeMultiDisplayTest, IsEnabled) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();

    // Check if the display is enabled
    ASSERT_TRUE(multiDisplay->isEnabled());
}

TEST_F(FakeMultiDisplayTest, DisplayEvents) {
    using namespace std::chrono_literals;
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();
    auto listener = std::make_shared<DisplayEventListener>();
    reinterpret_cast<CallbackEventSource<DisplayEvent>*>(multiDisplay)->addListener(listener);

    // Create a new display
    auto result = multiDisplay->createDisplay(1, 800, 600);
    ASSERT_TRUE(result.ok());

    ASSERT_TRUE(listener->waitForEvent(1, 1s));
    ASSERT_EQ(listener->events.size(), 1);
    ASSERT_TRUE(listener->events[0].isAddedEvent());
    ASSERT_EQ(listener->events[0].display().lock()->id(), 1);

    // Erase the display
    auto eraseResult = multiDisplay->eraseDisplay(1);
    ASSERT_TRUE(eraseResult.ok());

    ASSERT_TRUE(listener->waitForEvent(2, 1s));
    ASSERT_EQ(listener->events.size(), 2);
    ASSERT_TRUE(listener->events[1].isDeletedEvent());
    ASSERT_EQ(listener->events[1].displayId(), 1);
}

TEST_F(FakeMultiDisplayTest, DisplayEventsAreOnTheEventLoop) {
    using namespace std::chrono_literals;
    // Get the singleton instance
    IMultiDisplay* multiDisplay = IMultiDisplay::instance();
    absl::Notification event;
    auto callback =
            android::base::eventing::makeScopedCallback(*multiDisplay, [&](const DisplayEvent& _) {
                ASSERT_TRUE(mLoop->isOnLoopThread())
                        << "Event should have been delivered on the event loop";
                event.Notify();
            });
    auto result = multiDisplay->createDisplay(1, 800, 600);
    ASSERT_TRUE(result.ok());
    event.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(FakeMultiDisplayTest, ResizeMaintainsPatternIntegrity) {
    constexpr int kWidth = 1080;
    constexpr int kHeight = 2400;

    // 1. Create Image
    ChessboardStrategy strategy;
    auto sourceImage = PixmanImagePtr(
            pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, kWidth, kHeight, nullptr, 0));
    ASSERT_NE(sourceImage.get(), nullptr);
    strategy.generate(sourceImage.get(), 0);

    // 2. Get Display HAL
    auto displayResult = mFakeMultiDisplay->getDisplay(0);
    ASSERT_TRUE(displayResult.ok());
    auto display = std::dynamic_pointer_cast<FakePixmanDisplay>(displayResult->lock());
    ASSERT_NE(display, nullptr);

    // 3. Update HAL State
    display->updateSourceImage(sourceImage.get());

    // 4. Trigger Surface Update
    display->updateSurface(0, 0, kWidth, kHeight);

    // 5. Retrieve Pixels
    std::vector<uint8_t> pixel_buffer(kWidth * kHeight * 4);
    size_t num_pixels = pixel_buffer.size();
    auto pixelsResult = display->getPixels(PixelFormat::RGBA8888, kWidth, kHeight, 0,
                                           pixel_buffer.data(), &num_pixels);
    ASSERT_TRUE(pixelsResult.ok());

    // 6. Validate Pattern
    auto validationImage = PixmanImagePtr(pixman_image_create_bits_no_clear(
            PIXMAN_a8r8g8b8, kWidth, kHeight, (uint32_t*)pixel_buffer.data(), kWidth * 4));
    ASSERT_NE(validationImage.get(), nullptr);
    EXPECT_TRUE(strategy.isGeneratedBy(validationImage.get(), 0));
}

TEST_F(FakeMultiDisplayTest, ResizeWithScaling) {
    constexpr int kInitialWidth = 1080;
    constexpr int kInitialHeight = 2400;

    // 1. Create Image
    ChessboardStrategy strategy;
    auto sourceImage = PixmanImagePtr(pixman_image_create_bits_no_clear(
            PIXMAN_a8r8g8b8, kInitialWidth, kInitialHeight, nullptr, 0));
    ASSERT_NE(sourceImage.get(), nullptr);
    strategy.generate(sourceImage.get(), 0);

    // 2. Get Display HAL
    auto displayResult = mFakeMultiDisplay->getDisplay(0);
    ASSERT_TRUE(displayResult.ok());
    auto display = std::dynamic_pointer_cast<FakePixmanDisplay>(displayResult->lock());
    ASSERT_NE(display, nullptr);

    // 3. Update HAL State
    display->updateSourceImage(sourceImage.get());
    display->updateSurface(0, 0, kInitialWidth, kInitialHeight);

    // 4. Resize to 540x1200 and validate
    constexpr int kResizeWidth1 = 540;
    constexpr int kResizeHeight1 = 1200;
    std::vector<uint8_t> pixel_buffer1(kResizeWidth1 * kResizeHeight1 * 4);
    size_t num_pixels1 = pixel_buffer1.size();
    auto pixelsResult1 = display->getPixels(PixelFormat::RGBA8888, kResizeWidth1, kResizeHeight1, 0,
                                            pixel_buffer1.data(), &num_pixels1);
    ASSERT_TRUE(pixelsResult1.ok());
    auto validationImage1 = PixmanImagePtr(
            pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, kResizeWidth1, kResizeHeight1,
                                              (uint32_t*)pixel_buffer1.data(), kResizeWidth1 * 4));
    ASSERT_NE(validationImage1.get(), nullptr);
    EXPECT_TRUE(strategy.isGeneratedBy(validationImage1.get(), 0));

    // 5. Resize to 541x1204 and validate
    constexpr int kResizeWidth2 = 541;
    constexpr int kResizeHeight2 = 1204;
    std::vector<uint8_t> pixel_buffer2(kResizeWidth2 * kResizeHeight2 * 4);
    size_t num_pixels2 = pixel_buffer2.size();
    auto pixelsResult2 = display->getPixels(PixelFormat::RGBA8888, kResizeWidth2, kResizeHeight2, 0,
                                            pixel_buffer2.data(), &num_pixels2);
    ASSERT_TRUE(pixelsResult2.ok());
    auto validationImage2 = PixmanImagePtr(
            pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, kResizeWidth2, kResizeHeight2,
                                              (uint32_t*)pixel_buffer2.data(), kResizeWidth2 * 4));
    ASSERT_NE(validationImage2.get(), nullptr);
    // Note: This is expected to fail, as the above ratio will cause shearing.
    // as we didn't *snap* the resize to a ratio we actually support.
    EXPECT_FALSE(strategy.isGeneratedBy(validationImage2.get(), 0));
}

// This test was used as a diagnostic tool to identify a shearing artifact
// that occurred during image scaling. The issue was traced back to rounding
// errors in pixman's scaling algorithm when the source and destination
// dimensions did not share a sufficiently simple ratio.
//
// The test iterates through various scaled widths, keeping the aspect ratio,
// to pinpoint the exact dimensions where the shearing (pattern corruption)
// begins. The insights from this test led to the implementation of a "snapping"
// mechanism that forces the scaled dimensions to a ratio that pixman can
// handle without introducing these rounding errors, thus preserving image
// integrity.
//
// It is disabled because it is a diagnostic test and not a regression test.
TEST_F(FakeMultiDisplayTest, DISABLED_ScalingFailureBoundaryTest) {
    constexpr int kInitialWidth = 1080;
    constexpr int kInitialHeight = 2400;

    // 1. Create Image
    ChessboardStrategy strategy;
    auto sourceImage = PixmanImagePtr(pixman_image_create_bits_no_clear(
            PIXMAN_a8r8g8b8, kInitialWidth, kInitialHeight, nullptr, 0));
    ASSERT_NE(sourceImage.get(), nullptr);
    strategy.generate(sourceImage.get(), 0);

    // 2. Get Display HAL
    auto displayResult = mFakeMultiDisplay->getDisplay(0);
    ASSERT_TRUE(displayResult.ok());
    auto display = std::dynamic_pointer_cast<FakePixmanDisplay>(displayResult->lock());
    ASSERT_NE(display, nullptr);

    // 3. Update HAL State
    display->updateSourceImage(sourceImage.get());
    display->updateSurface(0, 0, kInitialWidth, kInitialHeight);

    // 4. Iterate and validate
    for (int w = kInitialWidth; w >= 530; --w) {
        auto [newWidth, newHeight] = display->resizeKeepAspectRatio(w, kInitialHeight);

        std::vector<uint8_t> buffer(newWidth * newHeight * 4);
        size_t bufferSize = buffer.size();
        auto result = display->getPixels(PixelFormat::RGBA8888, newWidth, newHeight, 0,
                                         buffer.data(), &bufferSize);
        ASSERT_TRUE(result.ok());

        auto validationImage = PixmanImagePtr(pixman_image_create_bits_no_clear(
                PIXMAN_a8r8g8b8, newWidth, newHeight, (uint32_t*)buffer.data(), newWidth * 4));
        ASSERT_NE(validationImage.get(), nullptr);

        bool success = strategy.isGeneratedBy(validationImage.get(), 0);
        std::cout << "Width: " << newWidth << ", Height: " << newHeight << " -> "
                  << (success ? "PASS" : "FAIL") << std::endl;
    }
}