// Copyright (C) 2025 The Android Open Source Project
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
#include <chrono>
#include <memory>
#include <vector>

#include "GrpcServiceTest.h"
#include "absl/container/flat_hash_map.h"

#include "android/emulation/control/DisplayService.h"
#include "android/goldfish/config/fake-avd.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/test/FakeMultiDisplay.h"
#include "goldfish/display/test/FakePixmanDisplay.h"

namespace android::emulation::control {

using ::goldfish::display::IMultiDisplay;
using ::goldfish::display::PixelFormat;
using ::goldfish::display::test::FakeMultiDisplay;
using ::goldfish::sensors::AndroidSensor;
using ::goldfish::sensors::PhysicalModel;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

using namespace std::chrono_literals;

class DisplayServiceTest : public GrcpServiceTest {
  protected:
    void SetUp() override {
        mPhysicalModel = std::make_unique<PhysicalModel>(mAvd.hw());
        mLoop = ::goldfish::async::ThreadedEventLoop::create(
                ::goldfish::async::LibuvEventLoop::create());
        mQemuLoop = ::goldfish::async::ThreadedEventLoop::create(
                ::goldfish::async::LibuvEventLoop::create());
        mMultiDisplay = std::make_unique<FakeMultiDisplay>(mLoop.get());
        mDisplayService =
                std::make_unique<DisplayServiceImpl>(mMultiDisplay.get(), mPhysicalModel.get());
        auto createResult = mMultiDisplay->createDisplay(1, 100, 50);
        ASSERT_TRUE(createResult.ok());

        GrcpServiceTest::SetUp();
    }

    // This starts the generation of fake display images on the given display id.
    void startFrames(int displayId) {
        using ::goldfish::display::test::ActiveFakePixmanDisplay;

        auto screen = mMultiDisplay->getDisplay(displayId);
        ASSERT_TRUE(screen.ok());

        auto display = screen->lock();
        ASSERT_TRUE(display);
        reinterpret_cast<ActiveFakePixmanDisplay*>(display.get())->start();
    }

    EmulatorController::Service* getService() override { return mDisplayService.get(); }

  protected:
    goldfish::FakeAvd mAvd;
    std::unique_ptr<PhysicalModel> mPhysicalModel;
    std::unique_ptr<::goldfish::async::EventLoop> mLoop;
    std::unique_ptr<::goldfish::async::EventLoop> mQemuLoop;
    std::unique_ptr<FakeMultiDisplay> mMultiDisplay;
    std::unique_ptr<DisplayServiceImpl> mDisplayService;

    // Maps rotation -> accelerometer values.
    absl::flat_hash_map<Rotation_SkinRotation, std::array<float, 3>> mRotationMap = {
        {Rotation::PORTRAIT, {0.0, 1.0, 0.0}},
        {Rotation::LANDSCAPE, {1.0, 0.0, 0.0}},
        {Rotation::REVERSE_PORTRAIT, {0.0, -1.0, 0.0}},
        {Rotation::REVERSE_LANDSCAPE, {-1.0, 0.0, 0.0}}};
};

TEST_F(DisplayServiceTest, GetScreenshotRGBA8888) {
    // Get a screenshot
    ImageFormat request;
    Image reply;
    request.set_display(1);
    request.set_format(ImageFormat::RGBA8888);

    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getScreenshot(context.get(), request, &reply));

    // Check the format
    EXPECT_EQ(reply.format().width(), 100);
    EXPECT_EQ(reply.format().height(), 50);
    EXPECT_EQ(reply.format().display(), 1);

    // Check if the screenshot has the correct data (at least one pixel)
    uint32_t* pixelData = reinterpret_cast<uint32_t*>(reply.mutable_image()->data());
    ASSERT_NE(pixelData[0] | pixelData[1] | pixelData[2] | pixelData[3], 0);
}

TEST_F(DisplayServiceTest, GetScreenshotRGB888) {
    // Get a screenshot
    Image reply;
    ImageFormat request;
    request.set_display(1);
    request.set_format(ImageFormat::RGB888);

    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getScreenshot(context.get(), request, &reply));

    // Check the format
    EXPECT_EQ(reply.format().width(), 100);
    EXPECT_EQ(reply.format().height(), 50);
    EXPECT_EQ(reply.format().display(), 1);

    // Check if the screenshot has the correct data (at least one pixel)
    uint8_t* pixelData = reinterpret_cast<uint8_t*>(reply.mutable_image()->data());
    ASSERT_NE(pixelData[0] | pixelData[1] | pixelData[2], 0);
}

TEST_F(DisplayServiceTest, GetScreenshotInvalidDisplay) {
    // Get a screenshot from an invalid display
    Image reply;
    ImageFormat request;
    request.set_display(99);
    request.set_format(ImageFormat::RGBA8888);

    auto context = getContextWithTimeout();
    Status status = mStub->getScreenshot(context.get(), request, &reply);
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(DisplayServiceTest, GetScreenshotScaling) {
    // Create a display
    auto createResult = mMultiDisplay->createDisplay(2, 400, 200);
    ASSERT_TRUE(createResult.ok());

    // Get a screenshot with scaling
    Image reply;
    ImageFormat request;
    request.set_display(2);
    request.set_format(ImageFormat::RGBA8888);
    request.set_width(200);
    request.set_height(100);

    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getScreenshot(context.get(), request, &reply));

    // Check the format
    EXPECT_EQ(reply.format().width(), 200);
    EXPECT_EQ(reply.format().height(), 100);
    EXPECT_EQ(reply.format().display(), 2);
}

TEST_F(DisplayServiceTest, GetDisplayConfigurations) {
    // Create a few displays
    auto createResult2 = mMultiDisplay->createDisplay(2, 200, 100);
    ASSERT_TRUE(createResult2.ok());

    // Get the display configurations
    Empty request;
    DisplayConfigurations reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getDisplayConfigurations(context.get(), request, &reply));

    // Check the number of displays
    EXPECT_EQ(reply.displays_size(), 3);

    // Check the properties of the displays
    bool foundDefault = false;
    bool found1 = false;
    bool found2 = false;
    for (const auto& display : reply.displays()) {
        if (display.display() == 0) {
            foundDefault = true;
            EXPECT_EQ(display.width(), 640);
            EXPECT_EQ(display.height(), 480);
        } else if (display.display() == 1) {
            found1 = true;
            EXPECT_EQ(display.width(), 100);
            EXPECT_EQ(display.height(), 50);
        } else if (display.display() == 2) {
            found2 = true;
            EXPECT_EQ(display.width(), 200);
            EXPECT_EQ(display.height(), 100);
        }
    }
    ASSERT_TRUE(foundDefault);
    ASSERT_TRUE(found1);
    ASSERT_TRUE(found2);
}

TEST_F(DisplayServiceTest, GetScreenshotNoUpscaling) {
    // Get a screenshot with scaling that is larger than the display
    Image reply;
    ImageFormat request;
    request.set_display(1);
    request.set_format(ImageFormat::RGBA8888);
    request.set_width(2000);
    request.set_height(1000);
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getScreenshot(context.get(), request, &reply));

    // Check the format. We should never scale above the device size.
    EXPECT_EQ(reply.format().width(), 100);
    EXPECT_EQ(reply.format().height(), 50);
    EXPECT_EQ(reply.format().display(), 1);
}

TEST_F(DisplayServiceTest, GetScreenshotNoSize) {
    // Get a screenshot with scaling that is larger than the display
    Image reply;
    ImageFormat request;
    request.set_display(1);
    request.set_format(ImageFormat::RGBA8888);
    request.set_width(0);
    request.set_height(0);
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getScreenshot(context.get(), request, &reply));

    // Check the format. We should never scale above the device size.
    EXPECT_EQ(reply.format().width(), 100);
    EXPECT_EQ(reply.format().height(), 50);
    EXPECT_EQ(reply.format().display(), 1);

    ASSERT_NE(reply.image().size(), 0);
}

}  // namespace android::emulation::control
