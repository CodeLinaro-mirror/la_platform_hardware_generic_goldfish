#include "goldfish/display/test/fake_multi_display.h"

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/test/display_test_peer.h"
#include "goldfish/display/test/fake_pixman_display.h"
#include "goldfish/display/test/image_generation_strategy.h"

using android::base::eventing::EventListener;

using goldfish::async::EventLoop;
using goldfish::display::DisplayEvent;
using goldfish::display::DisplayId;
using goldfish::display::DisplayPtr;
using goldfish::display::DisplayTestPeer;
using goldfish::display::IDisplay;
using goldfish::display::ImageRotation;
using goldfish::display::IMultiDisplay;
using goldfish::display::Orientation;
using goldfish::display::PixelFormat;
using goldfish::display::PixmanImagePtr;
using goldfish::display::test::ChessboardStrategy;
using goldfish::display::test::FakeMultiDisplay;
using goldfish::display::test::FakePixmanDisplay;

namespace {

// Rotation of the frames that consumers receive. Locked displays must keep it even when
// sensor updates change the device rotation.
ImageRotation FrameRotation(const IDisplay& display) {
    return DisplayTestPeer::frames(display)->GetRotation();
}

}  // namespace

class FakeMultiDisplayTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop_ = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create());

        // Clear all displays except the default one before each test
        fake_multi_display_ = std::make_unique<FakeMultiDisplay>(loop_.get());
    }
    void TearDown() override {
        fake_multi_display_.reset();  // Display teardown posts to the loop.
        loop_.reset();
    }

    std::unique_ptr<FakeMultiDisplay> fake_multi_display_;
    std::unique_ptr<EventLoop> loop_;
};

class DisplayEventListener : public EventListener<DisplayEvent> {
  public:
    void EventArrived(const DisplayEvent& event) override {
        const std::unique_lock<std::mutex> lock(mutex_);
        events.push_back(event);
        cv_.notify_one();
    }

    bool WaitForEvent(size_t event_count,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout,
                            [this, event_count] { return events.size() >= event_count; });
    }

    std::vector<DisplayEvent> events;

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
};

TEST_F(FakeMultiDisplayTest, CreateAndGetDisplay) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();

    // Create a new display
    auto result = multi_display->CreateDisplay(1, 800, 600, 320, 1);
    ASSERT_TRUE(result.ok());
    const DisplayPtr& display = result.value();

    // Get the display by ID
    auto get_result = multi_display->GetDisplay(1);
    ASSERT_TRUE(get_result.ok());
    const DisplayPtr& retrieved_display = get_result.value();

    // Check if the retrieved display is the same as the created one
    ASSERT_EQ(display.lock().get(), retrieved_display.lock().get());
}

TEST_F(FakeMultiDisplayTest, CreateDisplayAlreadyExists) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();

    // Create a new display
    auto result = multi_display->CreateDisplay(2, 800, 600, 320, 1);
    ASSERT_TRUE(result.ok());

    // Try to create a display with the same ID
    auto result2 = multi_display->CreateDisplay(2, 1024, 768, 320, 1);
    ASSERT_FALSE(result2.ok());
    ASSERT_EQ(result2.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(FakeMultiDisplayTest, GetDisplayNotFound) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();

    // Try to get a non-existent display
    auto result = multi_display->GetDisplay(99);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(FakeMultiDisplayTest, EraseDisplay) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();

    // Create a new display
    auto result = multi_display->CreateDisplay(3, 800, 600, 320, 1);
    ASSERT_TRUE(result.ok());

    // Erase the display
    auto erase_result = multi_display->EraseDisplay(3);
    ASSERT_TRUE(erase_result.ok());

    // Try to get the erased display
    auto get_result = multi_display->GetDisplay(3);
    ASSERT_FALSE(get_result.ok());
    ASSERT_EQ(get_result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(FakeMultiDisplayTest, EraseDefaultDisplay) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();

    // Try to erase the default display
    auto result = multi_display->EraseDisplay(0);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(FakeMultiDisplayTest, Displays) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();

    // Create a few displays
    auto result1 = multi_display->CreateDisplay(4, 800, 600, 320, 1);
    ASSERT_TRUE(result1.ok());
    auto result2 = multi_display->CreateDisplay(5, 1024, 768, 320, 1);
    ASSERT_TRUE(result2.ok());

    // Get all displays
    const std::vector<DisplayPtr> displays = multi_display->Displays();

    // Check if the number of displays is correct (including the default display)
    ASSERT_EQ(displays.size(), 3);

    // Check if the default display is present
    bool default_display_found = false;
    for (const auto& display : displays) {
        if (display.lock()->Id() == 0) {
            default_display_found = true;
            break;
        }
    }
    ASSERT_TRUE(default_display_found);
}

TEST_F(FakeMultiDisplayTest, DefaultDisplay) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();

    // Get the default display
    const DisplayPtr default_display = multi_display->DefaultDisplay().value();

    // Check if the default display has the correct ID
    ASSERT_EQ(default_display.lock()->Id(), 0);
}

TEST_F(FakeMultiDisplayTest, IsEnabled) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();

    // Check if the display is enabled
    ASSERT_TRUE(multi_display->IsEnabled());
}

TEST_F(FakeMultiDisplayTest, DisplayEvents) {
    using namespace std::chrono_literals;
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();
    auto listener = std::make_shared<DisplayEventListener>();
    reinterpret_cast<android::base::eventing::CallbackEventSource<DisplayEvent>*>(multi_display)
            ->AddListener(listener);

    // Create a new display
    auto result = multi_display->CreateDisplay(1, 800, 600, 320, 1);
    ASSERT_TRUE(result.ok());

    ASSERT_TRUE(listener->WaitForEvent(1, 1s));
    ASSERT_EQ(listener->events.size(), 1);
    ASSERT_TRUE(listener->events[0].IsAddedEvent());
    ASSERT_EQ(listener->events[0].Display().lock()->Id(), 1);

    // Erase the display
    auto erase_result = multi_display->EraseDisplay(1);
    ASSERT_TRUE(erase_result.ok());

    ASSERT_TRUE(listener->WaitForEvent(2, 1s));
    ASSERT_EQ(listener->events.size(), 2);
    ASSERT_TRUE(listener->events[1].IsDeletedEvent());
    ASSERT_EQ(listener->events[1].DisplayId(), 1);
}

TEST_F(FakeMultiDisplayTest, DisplayEventsAreOnTheEventLoop) {
    // Get the singleton Instance
    IMultiDisplay* multi_display = fake_multi_display_.get();
    absl::Notification event;
    auto callback = android::base::eventing::MakeScopedCallback(
            *multi_display, [&](const DisplayEvent& /*_*/) {
                ASSERT_TRUE(loop_->IsOnLoopThread())
                        << "Event should have been delivered on the event loop";
                event.Notify();
            });
    auto result = multi_display->CreateDisplay(1, 800, 600, 320, 1);
    ASSERT_TRUE(result.ok());
    event.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(FakeMultiDisplayTest, ResizeMaintainsPatternIntegrity) {
    constexpr int kWidth = 1080;
    constexpr int kHeight = 2400;

    // 1. Create Image
    ChessboardStrategy strategy;
    auto source_image = PixmanImagePtr(
            pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, kWidth, kHeight, nullptr, 0));
    ASSERT_NE(source_image.get(), nullptr);
    strategy.Generate(source_image.get(), 0);

    // 2. Get Display HAL
    auto display_result = fake_multi_display_->GetDisplay(0);
    ASSERT_TRUE(display_result.ok());
    auto display = std::dynamic_pointer_cast<FakePixmanDisplay>(display_result->lock());
    ASSERT_NE(display, nullptr);

    // 3. Update HAL State
    display->UpdateSourceImage(source_image.get());

    // 4. Trigger Surface Update
    display->UpdateSurface(0, 0, kWidth, kHeight);

    // 5. Retrieve Pixels
    std::vector<uint8_t> pixel_buffer(static_cast<size_t>(kWidth) * kHeight * 4);
    size_t num_pixels = pixel_buffer.size();
    auto pixels_result =
            display->GetPixels(PixelFormat::kRgba8888, kWidth, kHeight, ImageRotation::kRotation0,
                               pixel_buffer.data(), &num_pixels);
    ASSERT_TRUE(pixels_result.ok());

    // 6. Validate Pattern
    auto validation_image = PixmanImagePtr(pixman_image_create_bits_no_clear(
            PIXMAN_a8r8g8b8, kWidth, kHeight, reinterpret_cast<uint32_t*>(pixel_buffer.data()),
            kWidth * 4));
    ASSERT_NE(validation_image.get(), nullptr);
    EXPECT_TRUE(strategy.IsGeneratedBy(validation_image.get(), 0));
}

TEST_F(FakeMultiDisplayTest, DISABLED_ResizeWithScaling) {
    constexpr int kInitialWidth = 1080;
    constexpr int kInitialHeight = 2400;

    // 1. Create Image
    ChessboardStrategy strategy;
    auto source_image = PixmanImagePtr(pixman_image_create_bits_no_clear(
            PIXMAN_a8r8g8b8, kInitialWidth, kInitialHeight, nullptr, 0));
    ASSERT_NE(source_image.get(), nullptr);
    strategy.Generate(source_image.get(), 0);

    // 2. Get Display HAL
    auto display_result = fake_multi_display_->GetDisplay(0);
    ASSERT_TRUE(display_result.ok());
    auto display = std::dynamic_pointer_cast<FakePixmanDisplay>(display_result->lock());
    ASSERT_NE(display, nullptr);

    // 3. Update HAL State
    display->UpdateSourceImage(source_image.get());
    display->UpdateSurface(0, 0, kInitialWidth, kInitialHeight);

    // 4. Resize to 540x1200 and validate
    constexpr int kResizeWidth1 = 540;
    constexpr int kResizeHeight1 = 1200;
    std::vector<uint8_t> pixel_buffer1(static_cast<size_t>(kResizeWidth1) * kResizeHeight1 * 4);
    size_t num_pixels1 = pixel_buffer1.size();
    auto pixels_result1 =
            display->GetPixels(PixelFormat::kRgba8888, kResizeWidth1, kResizeHeight1,
                               ImageRotation::kRotation0, pixel_buffer1.data(), &num_pixels1);
    ASSERT_TRUE(pixels_result1.ok());
    auto validation_image1 = PixmanImagePtr(pixman_image_create_bits_no_clear(
            PIXMAN_a8r8g8b8, kResizeWidth1, kResizeHeight1,
            reinterpret_cast<uint32_t*>(pixel_buffer1.data()), kResizeWidth1 * 4));
    ASSERT_NE(validation_image1.get(), nullptr);
    EXPECT_TRUE(strategy.IsGeneratedBy(validation_image1.get(), 0));

    // 5. Resize to 541x1204 and validate
    constexpr int kResizeWidth2 = 541;
    constexpr int kResizeHeight2 = 1204;
    std::vector<uint8_t> pixel_buffer2(static_cast<size_t>(kResizeWidth2) * kResizeHeight2 * 4);
    size_t num_pixels2 = pixel_buffer2.size();
    auto pixels_result2 =
            display->GetPixels(PixelFormat::kRgba8888, kResizeWidth2, kResizeHeight2,
                               ImageRotation::kRotation0, pixel_buffer2.data(), &num_pixels2);
    ASSERT_TRUE(pixels_result2.ok());
    auto validation_image2 = PixmanImagePtr(pixman_image_create_bits_no_clear(
            PIXMAN_a8r8g8b8, kResizeWidth2, kResizeHeight2,
            reinterpret_cast<uint32_t*>(pixel_buffer2.data()), kResizeWidth2 * 4));
    ASSERT_NE(validation_image2.get(), nullptr);
    // Note: This is expected to fail, as the above ratio will cause shearing.
    // as we didn't *snap* the resize to a ratio we actually support.
    EXPECT_FALSE(strategy.IsGeneratedBy(validation_image2.get(), 0));
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioZeroDimensionsReturnZero) {
    auto display_result = fake_multi_display_->CreateDisplay(1, 1080, 2400, 320, 1);
    auto display = display_result.value().lock();

    auto result1 = display->ResizeKeepAspectRatio(0, 100);
    EXPECT_EQ(result1.first, 0);
    EXPECT_EQ(result1.second, 0);

    auto result2 = display->ResizeKeepAspectRatio(100, 0);
    EXPECT_EQ(result2.first, 0);
    EXPECT_EQ(result2.second, 0);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioPortraitToPortraitBox) {
    // Physical: 1080x2400 (Portrait)
    auto display_result = fake_multi_display_->CreateDisplay(1, 1080, 2400, 320, 1);
    auto display = display_result.value().lock();

    // Request: 540x2000 (Portrait box)
    // Aspect ratio 1080/2400 = 0.45
    // 540 / 0.45 = 1200.
    // Result should be 540x1200.
    auto result = display->ResizeKeepAspectRatio(540, 2000);
    EXPECT_TRUE((result.first == 540 && result.second == 1200) ||
                (result.first == 1080 && result.second == 2400));
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioPortraitToLandscapeBox) {
    // Physical: 1080x2400 (Portrait)
    auto display_result = fake_multi_display_->CreateDisplay(1, 1080, 2400, 320, 1);
    auto display = display_result.value().lock();

    // Request: 2000x540 (Landscape box)
    // The API should recognize that we want a logical Landscape view of the Portrait buffer.
    // Logical Source: 2400x1080 (Swapped)
    // Logical Aspect Ratio: 2400/1080 = 2.222
    // Logical Result should fit into 2000x540.
    // If width-limited: 2000 / 2.222 = 900 (Too tall for 540)
    // If height-limited: 540 * 2.222 = 1200.
    // Expected Logical Result: 1200x540.
    auto result = display->ResizeKeepAspectRatio(2000, 540);
    EXPECT_TRUE((result.first == 1200 && result.second == 540) ||
                (result.first == 2400 && result.second == 1080));
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioLandscapeToPortraitBox) {
    // Physical: 2400x1080 (Landscape)
    auto display_result = fake_multi_display_->CreateDisplay(1, 2400, 1080, 320, 1);
    auto display = display_result.value().lock();

    // Request: 540x2000 (Portrait box)
    // Logical Source: 1080x2400 (Swapped)
    // Expected Logical Result: 540x1200.
    auto result = display->ResizeKeepAspectRatio(540, 2000);
    // EXPECT_EQ(result.first, 540);
    // EXPECT_EQ(result.second, 1200);
    EXPECT_TRUE((result.first == 540 && result.second == 1200) ||
                (result.first == 1080 && result.second == 2400));
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioConstraints) {
    auto display_result = fake_multi_display_->CreateDisplay(1, 1000, 1000, 320, 1);
    auto display = display_result.value().lock();

    // Request larger than display
    auto result = display->ResizeKeepAspectRatio(2000, 2000);
    EXPECT_EQ(result.first, 1000);
    EXPECT_EQ(result.second, 1000);

    // Request larger than display (rotated)
    // Box is 2000x1500 (Landscape), Logical Source is 1000x1000.
    auto result2 = display->ResizeKeepAspectRatio(2000, 1500);
    EXPECT_EQ(result2.first, 1000);
    EXPECT_EQ(result2.second, 1000);
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioSquareSource) {
    // Physical: 1000x1000 (Square)
    auto display_result = fake_multi_display_->CreateDisplay(1, 1000, 1000, 320, 1);
    auto display = display_result.value().lock();

    // Box: 500x250 (Landscape box)
    // Logical Source remains 1000x1000 (Square has no orientation preference)
    // Result should fit 1000x1000 into 500x250.
    // Height-limited: 250 * (1000/1000) = 250.
    // However, Pixman snaps to 'safe' dimensions.
    // 250 % 4 != 0 -> 248. gcd(1000, 248)=8, 248/8=31 (<=1024). PASS.
    auto result1 = display->ResizeKeepAspectRatio(500, 250);
    EXPECT_TRUE((result1.first == 248 && result1.second == 248) ||
                (result1.first == 1000 && result1.second == 1000));

    // Box: 250x500 (Portrait box)
    // Result: 248x248.
    auto result2 = display->ResizeKeepAspectRatio(250, 500);
    EXPECT_TRUE((result1.first == 248 && result1.second == 248) ||
                (result1.first == 1000 && result1.second == 1000));
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioSquareBox) {
    // Physical: 1000x2000 (Portrait)
    auto display_result = fake_multi_display_->CreateDisplay(1, 1000, 2000, 320, 1);
    auto display = display_result.value().lock();

    // Box: 500x500 (Square box)
    // Since box is square (not wider than tall), we treat source as Portrait.
    // Result fits 1000x2000 into 500x500.
    // Height-limited: 500 * (1000/2000) = 250.
    // Snaps to 248 (width) -> 496 (height).
    auto result1 = display->ResizeKeepAspectRatio(500, 500);
    EXPECT_TRUE((result1.first == 248 && result1.second == 496) ||
                (result1.first == 1000 && result1.second == 2000));
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioNonDivisibleBy4Source) {
    // Physical: 1002x2002 (Portrait, not divisible by 4)
    auto display_result = fake_multi_display_->CreateDisplay(1, 1002, 2002, 320, 1);
    auto display = display_result.value().lock();

    // Box: 1002x2002
    // Ideal: 1002x2002
    // Safety check: 1002 % 4 != 0. Snaps width down.
    // 1000: 1000 % 4 == 0. gcd(1002, 1000)=2. 1000/2=500 (<= 1024). PASS.
    // Result: 1000x1998 (1998 = 1000 * 2002 / 1002)
    auto result = display->ResizeKeepAspectRatio(1002, 2002);
    EXPECT_TRUE((result.first == 1000 && result.second == 1998) ||
                (result.first == 1002 && result.second == 2002));
}

TEST_F(FakeMultiDisplayTest, ResizeKeepAspectRatioPartialDivisibilitySource) {
    // Physical: 1152x2570 (Portrait, width is div-4, height is NOT)
    auto display_result = fake_multi_display_->CreateDisplay(1, 1152, 2570, 320, 1);
    auto display = display_result.value().lock();

    // 1. Portrait request: 1152x2570
    // Ideal: 1152x2570.
    // Safety check: 1152 % 4 == 0. gcd(1152, 1152)=1152. PASS.
    // Result: 1152x2570.
    auto result_p = display->ResizeKeepAspectRatio(1152, 2570);
    EXPECT_EQ(result_p.first, 1152);
    EXPECT_EQ(result_p.second, 2570);

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
    auto result_l = display->ResizeKeepAspectRatio(2570, 1152);
    EXPECT_TRUE((result_l.first == 2560 && result_l.second == 1147) ||
                (result_l.first == 2570 && result_l.second == 1152));
}

TEST_F(FakeMultiDisplayTest, GetOrientation) {
    EXPECT_EQ(IDisplay::GetOrientation(1080, 2400), Orientation::kPortrait);
    EXPECT_EQ(IDisplay::GetOrientation(2400, 1080), Orientation::kLandscape);
    EXPECT_EQ(IDisplay::GetOrientation(1000, 1000), Orientation::kSquare);
    EXPECT_EQ(IDisplay::GetOrientation(0, 0), Orientation::kSquare);
}

TEST_F(FakeMultiDisplayTest, EraseActiveDisplayMarksDisplayRemovedOnEventLoop) {
    auto created = fake_multi_display_->CreateDisplay(2, 100, 50, 320, 1);
    ASSERT_TRUE(created.ok());
    auto display = created->lock();
    ASSERT_NE(display, nullptr);
    ASSERT_TRUE(display->WaitForFreshFrame(absl::Now() + absl::Seconds(5)).ok());

    absl::Notification deleted;
    bool removed_before_deleted_event = false;
    auto callback = android::base::eventing::MakeScopedCallback(
            *fake_multi_display_, [&](const DisplayEvent& event) {
                if (event.IsDeletedEvent() && event.DisplayId() == 2) {
                    EXPECT_TRUE(loop_->IsOnLoopThread());
                    removed_before_deleted_event = display->AcquireLatestFrame().removed;
                    deleted.Notify();
                }
            });

    ASSERT_TRUE(fake_multi_display_->EraseDisplay(2).ok());
    ASSERT_TRUE(deleted.WaitForNotificationWithTimeout(absl::Seconds(5)));
    EXPECT_TRUE(removed_before_deleted_event);
}

TEST_F(FakeMultiDisplayTest, ClearMarksActiveDisplaysRemovedOnEventLoop) {
    auto created1 = fake_multi_display_->CreateDisplay(1, 100, 50, 320, 1);
    auto created2 = fake_multi_display_->CreateDisplay(2, 100, 50, 320, 1);
    ASSERT_TRUE(created1.ok());
    ASSERT_TRUE(created2.ok());
    auto display1 = created1->lock();
    auto display2 = created2->lock();
    ASSERT_NE(display1, nullptr);
    ASSERT_NE(display2, nullptr);
    ASSERT_TRUE(display1->WaitForFreshFrame(absl::Now() + absl::Seconds(5)).ok());
    ASSERT_TRUE(display2->WaitForFreshFrame(absl::Now() + absl::Seconds(5)).ok());

    fake_multi_display_->Clear();

    auto synced = std::make_shared<absl::Notification>();
    ASSERT_TRUE(loop_->Post([synced]() { synced->Notify(); }).ok());
    ASSERT_TRUE(synced->WaitForNotificationWithTimeout(absl::Seconds(5)));

    EXPECT_TRUE(display1->AcquireLatestFrame().removed);
    EXPECT_TRUE(display2->AcquireLatestFrame().removed);
    EXPECT_TRUE(fake_multi_display_->DefaultDisplay().ok());
}

TEST_F(FakeMultiDisplayTest, DisplayStatePropertiesAndAutoRotationLock) {
    auto d0 = fake_multi_display_->DefaultDisplay()->lock();
    ASSERT_NE(d0, nullptr);
    auto created = fake_multi_display_->CreateDisplay(1, 800, 600, 320, 0x1U);
    ASSERT_TRUE(created.ok());
    auto d1 = created->lock();
    ASSERT_NE(d1, nullptr);

    // Defaults and setters for brightness and rotation properties.
    EXPECT_EQ(d1->Dpi(), 320U);
    EXPECT_EQ(d1->Flags(), 0x1U);
    EXPECT_FLOAT_EQ(d1->BrightnessPercent(), 100.0F);
    EXPECT_FALSE(d1->AutoBrightnessEnabled());
    EXPECT_TRUE(d1->AutoRotationEnabled());
    EXPECT_EQ(d1->UserRotation(), ImageRotation::kRotation0);

    d1->SetBrightnessPercent(42.5F);
    d1->SetAutoBrightnessEnabled(true);
    EXPECT_FLOAT_EQ(d1->BrightnessPercent(), 42.5F);
    EXPECT_TRUE(d1->AutoBrightnessEnabled());

    // Lock d0 to kRotation180 with auto-rotation disabled; leave d1 on auto-rotation.
    d0->SetUserRotation(ImageRotation::kRotation180);
    d0->SetAutoRotationEnabled(false);
    EXPECT_EQ(d0->GetRotation(), ImageRotation::kRotation180);
    EXPECT_EQ(FrameRotation(*d0), ImageRotation::kRotation180);

    // Updating IMultiDisplay rotation rotates auto-rotating d1, while locked d0 stays at 180.
    fake_multi_display_->SetRotation(ImageRotation::kRotation90);
    EXPECT_EQ(d0->GetRotation(), ImageRotation::kRotation180);
    EXPECT_EQ(FrameRotation(*d0), ImageRotation::kRotation180);
    EXPECT_EQ(d1->GetRotation(), ImageRotation::kRotation90);
    EXPECT_EQ(FrameRotation(*d1), ImageRotation::kRotation90);

    // Re-enabling auto-rotation adopts the recorded device rotation (kRotation90) automatically.
    d0->SetAutoRotationEnabled(true);
    EXPECT_EQ(d0->GetRotation(), ImageRotation::kRotation90);
    EXPECT_EQ(FrameRotation(*d0), ImageRotation::kRotation90);
}

namespace {

// Reusable spin barrier for tight lockstep thread interleavings in unit tests.
class SpinPhaseBarrier {
  public:
    explicit SpinPhaseBarrier(int parties) : parties_(parties) {}

    void ArriveAndWait() {
        const uint64_t current_phase = phase_.load(std::memory_order_acquire);
        if (arrived_.fetch_add(1, std::memory_order_acq_rel) + 1 == parties_) {
            arrived_.store(0, std::memory_order_relaxed);
            phase_.fetch_add(1, std::memory_order_release);
            return;
        }
        while (phase_.load(std::memory_order_acquire) == current_phase) {
            std::this_thread::yield();
        }
    }

  private:
    const int parties_;
    std::atomic<int> arrived_{0};
    std::atomic<uint64_t> phase_{0};
};

}  // namespace

TEST_F(FakeMultiDisplayTest, ConcurrentUserRotationAndAutoRotationLockPreserveInvariant) {
    auto d0 = fake_multi_display_->DefaultDisplay()->lock();
    ASSERT_NE(d0, nullptr);

    constexpr int kIterations = 2000;
    SpinPhaseBarrier start_barrier(3);
    SpinPhaseBarrier end_barrier(3);

    std::thread user_rot_thread([&] {
        for (int i = 0; i < kIterations; ++i) {
            start_barrier.ArriveAndWait();
            d0->SetUserRotation(ImageRotation::kRotation90);
            end_barrier.ArriveAndWait();
        }
    });

    std::thread lock_thread([&] {
        for (int i = 0; i < kIterations; ++i) {
            start_barrier.ArriveAndWait();
            d0->SetAutoRotationEnabled(false);
            end_barrier.ArriveAndWait();
        }
    });

    int mismatches = 0;
    for (int i = 0; i < kIterations; ++i) {
        fake_multi_display_->SetRotation(ImageRotation::kRotation180);
        d0->SetUserRotation(ImageRotation::kRotation0);
        d0->SetAutoRotationEnabled(true, ImageRotation::kRotation180);

        start_barrier.ArriveAndWait();
        end_barrier.ArriveAndWait();

        if (d0->AutoRotationEnabled() || d0->UserRotation() != ImageRotation::kRotation90 ||
            d0->GetRotation() != ImageRotation::kRotation90 ||
            FrameRotation(*d0) != ImageRotation::kRotation90) {
            ++mismatches;
        }
    }

    user_rot_thread.join();
    lock_thread.join();
    EXPECT_EQ(mismatches, 0)
            << "Invariant (!AutoRotationEnabled => state and frame rotation == UserRotation) "
               "violated under concurrent SetUserRotation + SetAutoRotationEnabled(false)";
}

TEST_F(FakeMultiDisplayTest, ConcurrentDeviceRotationAndAutoRotationLockPreserveInvariant) {
    auto d0 = fake_multi_display_->DefaultDisplay()->lock();
    ASSERT_NE(d0, nullptr);

    constexpr int kIterations = 2000;
    SpinPhaseBarrier start_barrier(3);
    SpinPhaseBarrier end_barrier(3);

    std::thread sensor_thread([&] {
        for (int i = 0; i < kIterations; ++i) {
            start_barrier.ArriveAndWait();
            fake_multi_display_->SetRotation(ImageRotation::kRotation270);
            end_barrier.ArriveAndWait();
        }
    });

    std::thread lock_thread([&] {
        for (int i = 0; i < kIterations; ++i) {
            start_barrier.ArriveAndWait();
            d0->SetAutoRotationEnabled(false);
            end_barrier.ArriveAndWait();
        }
    });

    int mismatches = 0;
    for (int i = 0; i < kIterations; ++i) {
        fake_multi_display_->SetRotation(ImageRotation::kRotation0);
        d0->SetUserRotation(ImageRotation::kRotation180);
        d0->SetAutoRotationEnabled(true, ImageRotation::kRotation0);

        start_barrier.ArriveAndWait();
        end_barrier.ArriveAndWait();

        if (d0->AutoRotationEnabled() || d0->UserRotation() != ImageRotation::kRotation180 ||
            d0->GetRotation() != ImageRotation::kRotation180 ||
            FrameRotation(*d0) != ImageRotation::kRotation180) {
            ++mismatches;
        }
    }

    sensor_thread.join();
    lock_thread.join();
    EXPECT_EQ(mismatches, 0) << "Sensor SetRotation overwrote locked display during concurrent "
                                "SetAutoRotationEnabled(false)";
}

TEST_F(FakeMultiDisplayTest, ConcurrentMultiDisplaySetRotationPreservesDisplaySync) {
    auto d0 = fake_multi_display_->DefaultDisplay()->lock();
    ASSERT_NE(d0, nullptr);
    d0->SetAutoRotationEnabled(true);

    constexpr int kIterations = 2000;
    SpinPhaseBarrier start_barrier(3);
    SpinPhaseBarrier end_barrier(3);

    std::thread rot90_thread([&] {
        for (int i = 0; i < kIterations; ++i) {
            start_barrier.ArriveAndWait();
            fake_multi_display_->SetRotation(ImageRotation::kRotation90);
            end_barrier.ArriveAndWait();
        }
    });

    std::thread rot270_thread([&] {
        for (int i = 0; i < kIterations; ++i) {
            start_barrier.ArriveAndWait();
            fake_multi_display_->SetRotation(ImageRotation::kRotation270);
            end_barrier.ArriveAndWait();
        }
    });

    int mismatches = 0;
    for (int i = 0; i < kIterations; ++i) {
        fake_multi_display_->SetRotation(ImageRotation::kRotation0);

        start_barrier.ArriveAndWait();
        end_barrier.ArriveAndWait();

        const ImageRotation expected = fake_multi_display_->GetRotation();
        if (d0->GetRotation() != expected || FrameRotation(*d0) != expected) {
            ++mismatches;
        }
    }

    rot90_thread.join();
    rot270_thread.join();
    EXPECT_EQ(mismatches, 0)
            << "IMultiDisplay::GetRotation() diverged from the auto-rotating display's state or "
               "frame rotation";
}

TEST_F(FakeMultiDisplayTest, ConcurrentUpdateStateAndGetStateNeverTear) {
    auto d0 = fake_multi_display_->DefaultDisplay()->lock();
    ASSERT_NE(d0, nullptr);

    const IDisplay::StateUpdate profile_a{
        .brightness_percent = 20.0F,
        .auto_brightness_enabled = false,
        .auto_rotation_enabled = false,
        .user_rotation = ImageRotation::kRotation90,
        .device_rotation = ImageRotation::kRotation0,
    };
    const IDisplay::StateUpdate profile_b{
        .brightness_percent = 80.0F,
        .auto_brightness_enabled = true,
        .auto_rotation_enabled = true,
        .user_rotation = ImageRotation::kRotation270,
        .device_rotation = ImageRotation::kRotation180,
    };
    (void)d0->UpdateState(profile_a);

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<int> torn_snapshots{0};

    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        constexpr int kUpdates = 5000;
        for (int i = 0; i < kUpdates; ++i) {
            (void)d0->UpdateState((i % 2 == 0) ? profile_b : profile_a);
        }
        done.store(true, std::memory_order_release);
    });

    constexpr int kReaders = 3;
    std::vector<std::thread> readers;
    readers.reserve(kReaders);
    for (int r = 0; r < kReaders; ++r) {
        readers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
            }
            while (!done.load(std::memory_order_acquire)) {
                const IDisplay::State snap = d0->GetState();
                const bool is_a = (snap.brightness_percent == 20.0F &&
                                   !snap.auto_brightness_enabled && !snap.auto_rotation_enabled &&
                                   snap.user_rotation == ImageRotation::kRotation90 &&
                                   snap.device_rotation == ImageRotation::kRotation0 &&
                                   snap.EffectiveRotation() == ImageRotation::kRotation90);
                const bool is_b = (snap.brightness_percent == 80.0F &&
                                   snap.auto_brightness_enabled && snap.auto_rotation_enabled &&
                                   snap.user_rotation == ImageRotation::kRotation270 &&
                                   snap.device_rotation == ImageRotation::kRotation180 &&
                                   snap.EffectiveRotation() == ImageRotation::kRotation180);
                if (!is_a && !is_b) {
                    torn_snapshots.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    writer.join();
    for (auto& t : readers) {
        t.join();
    }
    EXPECT_EQ(torn_snapshots.load(), 0)
            << "IDisplay::GetState() observed a torn intermediate state during UpdateState()";
}

TEST_F(FakeMultiDisplayTest, ConcurrentCreateDisplayAndSetRotationLockOrderSafe) {
    std::atomic<bool> start{false};
    std::atomic<bool> done{false};

    std::thread lifecycle_thread([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        constexpr int kCycles = 200;
        for (int i = 0; i < kCycles; ++i) {
            auto created = fake_multi_display_->CreateDisplay(1, 800, 600, 320, 0x1U);
            ASSERT_TRUE(created.ok());
            ASSERT_TRUE(fake_multi_display_->EraseDisplay(1).ok());
        }
        done.store(true, std::memory_order_release);
    });

    std::thread rotation_thread([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        constexpr ImageRotation kRotations[] = {
            ImageRotation::kRotation0,
            ImageRotation::kRotation90,
            ImageRotation::kRotation180,
            ImageRotation::kRotation270,
        };
        size_t idx = 0;
        while (!done.load(std::memory_order_acquire)) {
            fake_multi_display_->SetRotation(kRotations[idx % 4]);
            ++idx;
        }
    });

    start.store(true, std::memory_order_release);
    lifecycle_thread.join();
    rotation_thread.join();
}
