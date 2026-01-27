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
        mLoop = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create());

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
    void EventArrived(const DisplayEvent& event) override {
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
    reinterpret_cast<CallbackEventSource<DisplayEvent>*>(multiDisplay)->AddListener(listener);

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
            android::base::eventing::MakeScopedCallback(*multiDisplay, [&](const DisplayEvent& _) {
                ASSERT_TRUE(mLoop->IsOnLoopThread())
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
    auto pixelsResult =
            display->getPixels(PixelFormat::RGBA8888, kWidth, kHeight, ImageRotation::kRotation0,
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
    auto pixelsResult1 =
            display->getPixels(PixelFormat::RGBA8888, kResizeWidth1, kResizeHeight1,
                               ImageRotation::kRotation0, pixel_buffer1.data(), &num_pixels1);
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
    auto pixelsResult2 =
            display->getPixels(PixelFormat::RGBA8888, kResizeWidth2, kResizeHeight2,
                               ImageRotation::kRotation0, pixel_buffer2.data(), &num_pixels2);
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
        size_t buffer_size = buffer.size();
        auto result = display->getPixels(PixelFormat::RGBA8888, newWidth, newHeight,
                                         ImageRotation::kRotation0, buffer.data(), &buffer_size);
        ASSERT_TRUE(result.ok());

        auto validationImage = PixmanImagePtr(pixman_image_create_bits_no_clear(
                PIXMAN_a8r8g8b8, newWidth, newHeight, (uint32_t*)buffer.data(), newWidth * 4));
        ASSERT_NE(validationImage.get(), nullptr);

        bool success = strategy.isGeneratedBy(validationImage.get(), 0);
        std::cout << "Width: " << newWidth << ", Height: " << newHeight << " -> "
                  << (success ? "PASS" : "FAIL") << std::endl;
    }
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_ZeroDimensionsReturnZero) {
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 1080, 2400);
    auto display = displayResult.value().lock();

    auto result1 = display->resizeKeepAspectRatio(0, 100);
    EXPECT_EQ(result1.first, 0);
    EXPECT_EQ(result1.second, 0);

    auto result2 = display->resizeKeepAspectRatio(100, 0);
    EXPECT_EQ(result2.first, 0);
    EXPECT_EQ(result2.second, 0);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_PortraitToPortraitBox) {
    // Physical: 1080x2400 (Portrait)
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 1080, 2400);
    auto display = displayResult.value().lock();

    // Request: 540x2000 (Portrait box)
    // Aspect ratio 1080/2400 = 0.45
    // 540 / 0.45 = 1200.
    // Result should be 540x1200.
    auto result = display->resizeKeepAspectRatio(540, 2000);
    EXPECT_EQ(result.first, 540);
    EXPECT_EQ(result.second, 1200);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_PortraitToLandscapeBox) {
    // Physical: 1080x2400 (Portrait)
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 1080, 2400);
    auto display = displayResult.value().lock();

    // Request: 2000x540 (Landscape box)
    // The API should recognize that we want a logical Landscape view of the Portrait buffer.
    // Logical Source: 2400x1080 (Swapped)
    // Logical Aspect Ratio: 2400/1080 = 2.222
    // Logical Result should fit into 2000x540.
    // If width-limited: 2000 / 2.222 = 900 (Too tall for 540)
    // If height-limited: 540 * 2.222 = 1200.
    // Expected Logical Result: 1200x540.
    auto result = display->resizeKeepAspectRatio(2000, 540);
    EXPECT_EQ(result.first, 1200);
    EXPECT_EQ(result.second, 540);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_LandscapeToPortraitBox) {
    // Physical: 2400x1080 (Landscape)
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 2400, 1080);
    auto display = displayResult.value().lock();

    // Request: 540x2000 (Portrait box)
    // Logical Source: 1080x2400 (Swapped)
    // Expected Logical Result: 540x1200.
    auto result = display->resizeKeepAspectRatio(540, 2000);
    EXPECT_EQ(result.first, 540);
    EXPECT_EQ(result.second, 1200);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_Constraints) {
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 1000, 1000);
    auto display = displayResult.value().lock();

    // Request larger than display
    auto result = display->resizeKeepAspectRatio(2000, 2000);
    EXPECT_EQ(result.first, 1000);
    EXPECT_EQ(result.second, 1000);

    // Request larger than display (rotated)
    // Box is 2000x1500 (Landscape), Logical Source is 1000x1000.
    auto result2 = display->resizeKeepAspectRatio(2000, 1500);
    EXPECT_EQ(result2.first, 1000);
    EXPECT_EQ(result2.second, 1000);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_SquareSource) {
    // Physical: 1000x1000 (Square)
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 1000, 1000);
    auto display = displayResult.value().lock();

    // Box: 500x250 (Landscape box)
    // Logical Source remains 1000x1000 (Square has no orientation preference)
    // Result should fit 1000x1000 into 500x250.
    // Height-limited: 250 * (1000/1000) = 250.
    // However, Pixman snaps to 'safe' dimensions.
    // 250 % 4 != 0 -> 248. gcd(1000, 248)=8, 248/8=31 (<=1024). PASS.
    auto result1 = display->resizeKeepAspectRatio(500, 250);
    EXPECT_EQ(result1.first, 248);
    EXPECT_EQ(result1.second, 248);

    // Box: 250x500 (Portrait box)
    // Result: 248x248.
    auto result2 = display->resizeKeepAspectRatio(250, 500);
    EXPECT_EQ(result2.first, 248);
    EXPECT_EQ(result2.second, 248);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_SquareBox) {
    // Physical: 1000x2000 (Portrait)
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 1000, 2000);
    auto display = displayResult.value().lock();

    // Box: 500x500 (Square box)
    // Since box is square (not wider than tall), we treat source as Portrait.
    // Result fits 1000x2000 into 500x500.
    // Height-limited: 500 * (1000/2000) = 250.
    // Snaps to 248 (width) -> 496 (height).
    auto result1 = display->resizeKeepAspectRatio(500, 500);
    EXPECT_EQ(result1.first, 248);
    EXPECT_EQ(result1.second, 496);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_NonDivisibleBy4Source) {
    // Physical: 1002x2002 (Portrait, not divisible by 4)
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 1002, 2002);
    auto display = displayResult.value().lock();

    // Box: 1002x2002
    // Ideal: 1002x2002
    // Safety check: 1002 % 4 != 0. Snaps width down.
    // 1000: 1000 % 4 == 0. gcd(1002, 1000)=2. 1000/2=500 (<= 1024). PASS.
    // Result: 1000x1998 (1998 = 1000 * 2002 / 1002)
    auto result = display->resizeKeepAspectRatio(1002, 2002);
    EXPECT_EQ(result.first, 1000);
    EXPECT_EQ(result.second, 1998);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatio_PartialDivisibilitySource) {
    // Physical: 1152x2570 (Portrait, width is div-4, height is NOT)
    auto displayResult = mFakeMultiDisplay->createDisplay(1, 1152, 2570);
    auto display = displayResult.value().lock();

    // 1. Portrait request: 1152x2570
    // Ideal: 1152x2570.
    // Safety check: 1152 % 4 == 0. gcd(1152, 1152)=1152. PASS.
    // Result: 1152x2570.
    auto resultP = display->resizeKeepAspectRatio(1152, 2570);
    EXPECT_EQ(resultP.first, 1152);
    EXPECT_EQ(resultP.second, 2570);

    // 2. Landscape request: 2570x1152
    // Logical Source: 2570x1152.
    // Ideal: 2570x1152.
    // Safety check (Logical-to-Physical):
    // The physical destination width is 2570.
    // Pixman requires widths to be a multiple of 4 and share a "simple" ratio
    // with the source to avoid rounding errors (denominator <= 1024).
    //
    // For a physical source dimension of 2570:
    // - 2568: 2568 % 4 == 0. gcd(2570, 2568) = 2.
    //   Denominator = 2570 / 2 = 1285 (Too complex, > 1024).
    // - 2564: 2564 % 4 == 0. gcd(2570, 2564) = 2.
    //   Denominator = 2570 / 2 = 1285 (Too complex).
    // - 2560: 2560 % 4 == 0. gcd(2570, 2560) = 10.
    //   Denominator = 2570 / 10 = 257 (Safe, <= 1024).
    //
    // Result: 2560x1147 (1147 = 2560 * 1152 / 2570).
    auto resultL = display->resizeKeepAspectRatio(2570, 1152);
    EXPECT_EQ(resultL.first, 2560);
    EXPECT_EQ(resultL.second, 1147);
}

TEST_F(FakeMultiDisplayTest, GetOrientation) {
    EXPECT_EQ(IDisplay::getOrientation(1080, 2400), Orientation::kPortrait);
    EXPECT_EQ(IDisplay::getOrientation(2400, 1080), Orientation::kLandscape);
    EXPECT_EQ(IDisplay::getOrientation(1000, 1000), Orientation::kSquare);
    EXPECT_EQ(IDisplay::getOrientation(0, 0), Orientation::kSquare);
}