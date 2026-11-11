// Copyright (C) 2026 The Android Open Source Project
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

#include "display/display_service_impl.h"

#include <glm/vec3.hpp>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/goldfish/fake_hardware_config.h"
#include "display/screenshot_capturer.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/abstract_multi_display.h"
#include "goldfish/display/cpu_read_pipeline.h"
#include "goldfish/display/pixman_image_ptr.h"
#include "goldfish/display/test/fake_multi_display.h"
#include "goldfish/display/test/fake_pixman_display.h"
#include "goldfish/physics/rotation.h"
#include "goldfish/sensors/foldable.h"
#include "goldfish/sensors/physical_model.h"
#include "goldfish/sensors/physical_parameter.h"
#include "goldfish/system/testing/fake_clocks.h"
#include "test/grpc_v2_service_test.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::v2::CreateDisplayRequest;
using ::android::emulation::v2::DeleteDisplayRequest;
using ::android::emulation::v2::DeleteDisplayResponse;
using ::android::emulation::v2::Display;
using ::android::emulation::v2::DisplayFlag;
using ::android::emulation::v2::DisplayService;
using ::android::emulation::v2::GetDisplayRequest;
using ::android::emulation::v2::GetScreenshotRequest;
using ::android::emulation::v2::ImageFormat;
using ::android::emulation::v2::ListDisplaysRequest;
using ::android::emulation::v2::ListDisplaysResponse;
using ::android::emulation::v2::Rotation;
using ::android::emulation::v2::Screenshot;
using ::android::emulation::v2::StreamDisplaysRequest;
using ::android::emulation::v2::StreamScreenshotsRequest;
using ::android::emulation::v2::UpdateDisplayRequest;
using ::goldfish::display::test::ActiveFakePixmanDisplay;
using ::goldfish::display::test::FakeMultiDisplay;
using ::goldfish::grpc::v2::test::FakeWriteReactor;

class DisplayServiceImplTest : public ::testing::Test {
  protected:
    void SetUp() override {
        hw_ = ::android::goldfish::FakeHardwareConfig::GetHwConfig();
        loop_ = async::ThreadedEventLoop::Create(async::LibuvEventLoop::Create());
        multi_display_ = std::make_unique<FakeMultiDisplay>(loop_.get());
        physical_model_ = std::make_unique<sensors::PhysicalModel>(hw_, virtual_clock_);
        StartServer(DisplayServiceImpl::Options{});
    }

    void TearDown() override {
        StopFrames();
        StopServer();
        physical_model_.reset();
        multi_display_.reset();
        loop_.reset();
    }

    void StartServer(DisplayServiceImpl::Options options) {
        StopServer();
        service_ = std::make_unique<DisplayServiceImpl>(*multi_display_, physical_model_.get(),
                                                        options, clock_);
        ::grpc::ServerBuilder builder;
        builder.RegisterService(service_.get());
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);
        channel_ = server_->InProcessChannel(::grpc::ChannelArguments());
        stub_ = DisplayService::NewStub(channel_);
    }

    void StopServer() {
        stub_.reset();
        channel_.reset();
        if (server_) {
            server_->Shutdown(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
            server_->Wait();
            server_.reset();
        }
        service_.reset();
    }

    void StartFrames(int display_id) {
        auto screen = multi_display_->GetDisplay(static_cast<uint8_t>(display_id));
        ASSERT_TRUE(screen.ok());
        auto disp = screen->lock();
        ASSERT_NE(disp, nullptr);
        static_cast<ActiveFakePixmanDisplay*>(disp.get())->Start();
    }

    void StopFrames() {
        if (!multi_display_) {
            return;
        }
        for (const auto& weak_disp : multi_display_->Displays()) {
            if (auto disp = weak_disp.lock()) {
                static_cast<ActiveFakePixmanDisplay*>(disp.get())->Stop();
            }
        }
    }

    ::android::goldfish::HardwareConfig hw_{};
    system::testing::FakeVirtualClock virtual_clock_;
    system::testing::FakeSteadyClock clock_{absl::Seconds(10)};
    std::unique_ptr<async::EventLoop> loop_;
    std::unique_ptr<FakeMultiDisplay> multi_display_;
    std::unique_ptr<sensors::PhysicalModel> physical_model_;
    std::unique_ptr<DisplayServiceImpl> service_;
    std::unique_ptr<::grpc::Server> server_;
    std::shared_ptr<::grpc::Channel> channel_;
    std::unique_ptr<DisplayService::Stub> stub_;
};

TEST_F(DisplayServiceImplTest, ListAndGetPrimaryDisplay) {
    ::grpc::ServerContext list_ctx;
    ListDisplaysRequest list_req;
    ListDisplaysResponse list_resp;
    ASSERT_TRUE(service_->ListDisplays(&list_ctx, &list_req, &list_resp).ok());
    ASSERT_EQ(list_resp.displays_size(), 1);

    const Display& primary = list_resp.displays(0);
    EXPECT_EQ(primary.display_id(), 0);
    EXPECT_TRUE(primary.primary());
    EXPECT_EQ(primary.width(), 640);
    EXPECT_EQ(primary.height(), 480);
    EXPECT_EQ(primary.density_dpi(), 420);
    EXPECT_EQ(primary.rotation(), Rotation::ROTATION_DEG0);
    EXPECT_FLOAT_EQ(primary.brightness_percent(), 100.0F);
    EXPECT_FALSE(primary.auto_brightness_enabled());
    EXPECT_TRUE(primary.auto_rotation_enabled());
    EXPECT_EQ(primary.user_rotation(), Rotation::ROTATION_DEG0);
    EXPECT_EQ(primary.flags_size(), 0);

    ::grpc::ServerContext get_ctx;
    GetDisplayRequest get_req;
    get_req.set_display_id(0);
    Display get_resp;
    ASSERT_TRUE(service_->GetDisplay(&get_ctx, &get_req, &get_resp).ok());
    EXPECT_EQ(get_resp.display_id(), 0);
    EXPECT_TRUE(get_resp.primary());
    EXPECT_EQ(get_resp.width(), 640);
    EXPECT_EQ(get_resp.height(), 480);

    // Non-existent display ID returns NOT_FOUND.
    ::grpc::ServerContext missing_ctx;
    GetDisplayRequest missing_req;
    missing_req.set_display_id(99);
    EXPECT_EQ(service_->GetDisplay(&missing_ctx, &missing_req, &get_resp).error_code(),
              ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(DisplayServiceImplTest, CreateListAndDeleteSecondaryDisplays) {
    // Auto-allocate secondary display ID when display_id == 0.
    CreateDisplayRequest create1;
    create1.set_width(1280);
    create1.set_height(720);
    create1.set_density_dpi(320);
    create1.add_flags(DisplayFlag::DISPLAY_FLAG_PRESENTATION);
    create1.add_flags(DisplayFlag::DISPLAY_FLAG_PUBLIC);
    Display created1;
    {
        ::grpc::ServerContext ctx;
        const auto status = service_->CreateDisplay(&ctx, &create1, &created1);
        ASSERT_TRUE(status.ok()) << status.error_message();
    }
    EXPECT_EQ(created1.display_id(), 1);
    EXPECT_FALSE(created1.primary());
    EXPECT_EQ(created1.width(), 1280);
    EXPECT_EQ(created1.height(), 720);
    EXPECT_EQ(created1.density_dpi(), 320);
    ASSERT_EQ(created1.flags_size(), 2);
    EXPECT_EQ(created1.flags(0), DisplayFlag::DISPLAY_FLAG_PUBLIC);
    EXPECT_EQ(created1.flags(1), DisplayFlag::DISPLAY_FLAG_PRESENTATION);

    // Create a secondary display with explicit display_id = 3.
    CreateDisplayRequest create3;
    create3.set_display_id(3);
    create3.set_width(1920);
    create3.set_height(1080);
    create3.set_density_dpi(480);
    Display created3;
    {
        ::grpc::ServerContext ctx;
        ASSERT_TRUE(service_->CreateDisplay(&ctx, &create3, &created3).ok());
    }
    EXPECT_EQ(created3.display_id(), 3);
    EXPECT_FALSE(created3.primary());

    // ListDisplays returns active displays sorted by display_id (0, 1, 3).
    ListDisplaysResponse list_resp;
    {
        ::grpc::ServerContext ctx;
        ListDisplaysRequest req;
        ASSERT_TRUE(service_->ListDisplays(&ctx, &req, &list_resp).ok());
    }
    ASSERT_EQ(list_resp.displays_size(), 3);
    EXPECT_EQ(list_resp.displays(0).display_id(), 0);
    EXPECT_TRUE(list_resp.displays(0).primary());
    EXPECT_EQ(list_resp.displays(1).display_id(), 1);
    EXPECT_FALSE(list_resp.displays(1).primary());
    EXPECT_EQ(list_resp.displays(2).display_id(), 3);
    EXPECT_FALSE(list_resp.displays(2).primary());

    // Duplicate display_id fails with ALREADY_EXISTS.
    {
        ::grpc::ServerContext ctx;
        EXPECT_EQ(service_->CreateDisplay(&ctx, &create3, &created3).error_code(),
                  ::grpc::StatusCode::ALREADY_EXISTS);
    }

    // Auto-allocate one more display (display_id 2) to reach max_supported_display_count (4).
    Display created2;
    {
        ::grpc::ServerContext ctx;
        ASSERT_TRUE(service_->CreateDisplay(&ctx, &create1, &created2).ok());
        EXPECT_EQ(created2.display_id(), 2);
    }

    // Creating a fifth display fails with RESOURCE_EXHAUSTED.
    {
        ::grpc::ServerContext ctx;
        EXPECT_EQ(service_->CreateDisplay(&ctx, &create1, &created2).error_code(),
                  ::grpc::StatusCode::RESOURCE_EXHAUSTED);
    }

    // Deleting display 0 fails with INVALID_ARGUMENT.
    {
        ::grpc::ServerContext ctx;
        DeleteDisplayRequest del0;
        del0.set_display_id(0);
        DeleteDisplayResponse del_resp;
        EXPECT_EQ(service_->DeleteDisplay(&ctx, &del0, &del_resp).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }

    // Deleting a non-existent display fails with NOT_FOUND.
    {
        ::grpc::ServerContext ctx;
        DeleteDisplayRequest del9;
        del9.set_display_id(9);
        DeleteDisplayResponse del_resp;
        EXPECT_EQ(service_->DeleteDisplay(&ctx, &del9, &del_resp).error_code(),
                  ::grpc::StatusCode::NOT_FOUND);
    }

    // Deleting display 1 succeeds and frees its slot.
    {
        ::grpc::ServerContext ctx;
        DeleteDisplayRequest del1;
        del1.set_display_id(1);
        DeleteDisplayResponse del_resp;
        ASSERT_TRUE(service_->DeleteDisplay(&ctx, &del1, &del_resp).ok());
    }
    {
        ::grpc::ServerContext ctx;
        GetDisplayRequest get1;
        get1.set_display_id(1);
        Display d;
        EXPECT_EQ(service_->GetDisplay(&ctx, &get1, &d).error_code(),
                  ::grpc::StatusCode::NOT_FOUND);
    }
}

TEST_F(DisplayServiceImplTest, CreateDisplayValidatesArgumentsAndPreconditions) {
    Display resp;

    // Non-positive width/height/density_dpi.
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest req;
        req.set_width(0);
        req.set_height(720);
        req.set_density_dpi(320);
        EXPECT_EQ(service_->CreateDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest req;
        req.set_width(1280);
        req.set_height(-10);
        req.set_density_dpi(320);
        EXPECT_EQ(service_->CreateDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest req;
        req.set_width(1280);
        req.set_height(720);
        req.set_density_dpi(0);
        EXPECT_EQ(service_->CreateDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }

    // Out-of-range display_id.
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest req;
        req.set_display_id(99);
        req.set_width(1280);
        req.set_height(720);
        req.set_density_dpi(320);
        EXPECT_EQ(service_->CreateDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }

    // Invalid DisplayFlag (DISPLAY_FLAG_UNSPECIFIED).
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest req;
        req.set_width(1280);
        req.set_height(720);
        req.set_density_dpi(320);
        req.add_flags(DisplayFlag::DISPLAY_FLAG_UNSPECIFIED);
        EXPECT_EQ(service_->CreateDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }

    // Virtual display creation disabled -> FAILED_PRECONDITION.
    DisplayServiceImpl::Options opts;
    opts.supports_virtual_display_creation = false;
    StartServer(opts);
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest req;
        req.set_width(1280);
        req.set_height(720);
        req.set_density_dpi(320);
        EXPECT_EQ(service_->CreateDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::FAILED_PRECONDITION);
    }
}

TEST_F(DisplayServiceImplTest, UpdateDisplayPropertiesAndRotationLock) {
    // Update brightness and lock rotation to ROTATION_DEG90 on display 0 without resizing.
    UpdateDisplayRequest req;
    req.set_display_id(0);
    req.set_brightness_percent(75.5F);
    req.set_auto_brightness_enabled(true);
    req.set_auto_rotation_enabled(false);
    req.set_user_rotation(Rotation::ROTATION_DEG90);

    Display updated;
    {
        ::grpc::ServerContext ctx;
        const auto status = service_->UpdateDisplay(&ctx, &req, &updated);
        ASSERT_TRUE(status.ok()) << status.error_message();
    }
    EXPECT_EQ(updated.width(), 640);
    EXPECT_EQ(updated.height(), 480);
    EXPECT_EQ(updated.density_dpi(), 420);
    EXPECT_FLOAT_EQ(updated.brightness_percent(), 75.5F);
    EXPECT_TRUE(updated.auto_brightness_enabled());
    EXPECT_FALSE(updated.auto_rotation_enabled());
    EXPECT_EQ(updated.user_rotation(), Rotation::ROTATION_DEG90);
    EXPECT_EQ(updated.rotation(), Rotation::ROTATION_DEG90);

    // Changing width, height, or density_dpi on primary display 0 fails with FAILED_PRECONDITION.
    {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest resize_req;
        resize_req.set_display_id(0);
        resize_req.set_width(1080);
        EXPECT_EQ(service_->UpdateDisplay(&ctx, &resize_req, &updated).error_code(),
                  ::grpc::StatusCode::FAILED_PRECONDITION);
    }
    {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest dpi_req;
        dpi_req.set_display_id(0);
        dpi_req.set_density_dpi(440);
        EXPECT_EQ(service_->UpdateDisplay(&ctx, &dpi_req, &updated).error_code(),
                  ::grpc::StatusCode::FAILED_PRECONDITION);
    }

    // Secondary virtual displays allow updating brightness and rotation while preserving flags.
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest create_req;
        create_req.set_display_id(1);
        create_req.set_width(800);
        create_req.set_height(600);
        create_req.set_density_dpi(240);
        create_req.add_flags(DisplayFlag::DISPLAY_FLAG_PRESENTATION);
        Display created_sec;
        ASSERT_TRUE(service_->CreateDisplay(&ctx, &create_req, &created_sec).ok());
    }
    {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest update_sec;
        update_sec.set_display_id(1);
        update_sec.set_brightness_percent(60.0F);
        update_sec.set_auto_rotation_enabled(false);
        update_sec.set_user_rotation(Rotation::ROTATION_DEG270);
        Display updated_sec;
        const auto status = service_->UpdateDisplay(&ctx, &update_sec, &updated_sec);
        ASSERT_TRUE(status.ok()) << status.error_message();
        EXPECT_EQ(updated_sec.display_id(), 1);
        EXPECT_EQ(updated_sec.width(), 800);
        EXPECT_EQ(updated_sec.height(), 600);
        EXPECT_EQ(updated_sec.density_dpi(), 240);
        EXPECT_FLOAT_EQ(updated_sec.brightness_percent(), 60.0F);
        EXPECT_FALSE(updated_sec.auto_rotation_enabled());
        EXPECT_EQ(updated_sec.user_rotation(), Rotation::ROTATION_DEG270);
        EXPECT_EQ(updated_sec.rotation(), Rotation::ROTATION_DEG270);
        ASSERT_EQ(updated_sec.flags_size(), 1);
        EXPECT_EQ(updated_sec.flags(0), DisplayFlag::DISPLAY_FLAG_PRESENTATION);
    }

    // PhysicalModel gravity changes to REVERSE_PORTRAIT (0, 1, 0), but auto_rotation_enabled is
    // false, so display 0 remains locked at ROTATION_DEG90.
    physical_model_->SetGravity(0.0F, 1.0F, 0.0F);
    {
        ::grpc::ServerContext ctx;
        GetDisplayRequest get_req;
        get_req.set_display_id(0);
        Display fetched;
        ASSERT_TRUE(service_->GetDisplay(&ctx, &get_req, &fetched).ok());
        EXPECT_EQ(fetched.rotation(), Rotation::ROTATION_DEG90);
    }

    // Re-enabling auto_rotation_enabled adopts the PhysicalModel's REVERSE_PORTRAIT
    // (ROTATION_DEG180).
    UpdateDisplayRequest auto_rot_req;
    auto_rot_req.set_display_id(0);
    auto_rot_req.set_auto_rotation_enabled(true);
    {
        ::grpc::ServerContext ctx;
        ASSERT_TRUE(service_->UpdateDisplay(&ctx, &auto_rot_req, &updated).ok());
        EXPECT_TRUE(updated.auto_rotation_enabled());
        EXPECT_EQ(updated.rotation(), Rotation::ROTATION_DEG180);
    }

    // Validation errors on UpdateDisplay.
    {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest bad_bright;
        bad_bright.set_display_id(0);
        bad_bright.set_brightness_percent(105.0F);
        EXPECT_EQ(service_->UpdateDisplay(&ctx, &bad_bright, &updated).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }
    {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest bad_rot;
        bad_rot.set_display_id(0);
        bad_rot.set_user_rotation(Rotation::ROTATION_UNSPECIFIED);
        EXPECT_EQ(service_->UpdateDisplay(&ctx, &bad_rot, &updated).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }
}

TEST_F(DisplayServiceImplTest, StreamDisplaysPushesInitialAndReactiveSnapshots) {
    auto writer = service_->StreamDisplaysForTesting<FakeWriteReactor<ListDisplaysResponse>>();
    ASSERT_EQ(writer->written_messages.size(), 1U);
    EXPECT_EQ(writer->written_messages.back().displays_size(), 1);
    writer->OnWriteDone(true);

    // Create secondary display 1 -> triggers a snapshot with 2 displays.
    CreateDisplayRequest create_req;
    create_req.set_display_id(1);
    create_req.set_width(800);
    create_req.set_height(600);
    create_req.set_density_dpi(240);
    Display created;
    {
        ::grpc::ServerContext ctx;
        ASSERT_TRUE(service_->CreateDisplay(&ctx, &create_req, &created).ok());
    }
    writer->OnWriteDone(true);
    ASSERT_GE(writer->written_messages.size(), 2U);
    EXPECT_EQ(writer->written_messages.back().displays_size(), 2);

    // Filtered stream watching only display 1 ignores updates to display 0.
    auto filtered_writer =
            service_->StreamDisplaysForTesting<FakeWriteReactor<ListDisplaysResponse>>(1);
    ASSERT_EQ(filtered_writer->written_messages.size(), 1U);
    ASSERT_EQ(filtered_writer->written_messages.back().displays_size(), 1);
    EXPECT_EQ(filtered_writer->written_messages.back().displays(0).display_id(), 1);
    filtered_writer->OnWriteDone(true);

    UpdateDisplayRequest update0;
    update0.set_display_id(0);
    update0.set_brightness_percent(50.0F);
    {
        ::grpc::ServerContext ctx;
        Display d0;
        ASSERT_TRUE(service_->UpdateDisplay(&ctx, &update0, &d0).ok());
    }
    EXPECT_EQ(filtered_writer->written_messages.size(), 1U);

    UpdateDisplayRequest update1;
    update1.set_display_id(1);
    update1.set_brightness_percent(40.0F);
    {
        ::grpc::ServerContext ctx;
        Display d1;
        ASSERT_TRUE(service_->UpdateDisplay(&ctx, &update1, &d1).ok());
    }
    filtered_writer->OnWriteDone(true);
    ASSERT_EQ(filtered_writer->written_messages.size(), 2U);
    EXPECT_FLOAT_EQ(filtered_writer->written_messages.back().displays(0).brightness_percent(),
                    40.0F);
}

TEST_F(DisplayServiceImplTest, GetScreenshotFormatsRotationAndSecurity) {
    // Default format (IMAGE_FORMAT_UNSPECIFIED) produces PNG with magic header 89 50 4E 47.
    {
        ::grpc::ServerContext ctx;
        GetScreenshotRequest req;
        req.set_display_id(0);
        Screenshot resp;
        ASSERT_TRUE(service_->GetScreenshot(&ctx, &req, &resp).ok());
        EXPECT_EQ(resp.width(), 640);
        EXPECT_EQ(resp.height(), 480);
        EXPECT_EQ(resp.row_stride_bytes(), 0);
        EXPECT_EQ(resp.rotation(), Rotation::ROTATION_DEG0);
        EXPECT_EQ(resp.presentation_time_nanos(), 10'000'000'000LL);
        EXPECT_FLOAT_EQ(resp.density_scale(), 420.0F / 160.0F);
        ASSERT_GE(resp.image_data().size(), 4U);
        EXPECT_EQ(static_cast<uint8_t>(resp.image_data()[0]), 0x89U);
        EXPECT_EQ(resp.image_data()[1], 'P');
        EXPECT_EQ(resp.image_data()[2], 'N');
        EXPECT_EQ(resp.image_data()[3], 'G');
    }

    // RAW_RGBA8888 format populates row_stride_bytes = width * 4 and exact byte count.
    {
        ::grpc::ServerContext ctx;
        GetScreenshotRequest req;
        req.set_display_id(0);
        req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
        Screenshot resp;
        ASSERT_TRUE(service_->GetScreenshot(&ctx, &req, &resp).ok());
        EXPECT_EQ(resp.width(), 640);
        EXPECT_EQ(resp.height(), 480);
        EXPECT_EQ(resp.row_stride_bytes(), 640 * 4);
        EXPECT_EQ(resp.image_data().size(), 640U * 480U * 4U);
    }

    // JPEG format produces valid JPEG SOI marker (FF D8 FF).
    {
        ::grpc::ServerContext ctx;
        GetScreenshotRequest req;
        req.set_display_id(0);
        req.set_format(ImageFormat::IMAGE_FORMAT_JPEG);
        Screenshot resp;
        ASSERT_TRUE(service_->GetScreenshot(&ctx, &req, &resp).ok());
        EXPECT_EQ(resp.width(), 640);
        EXPECT_EQ(resp.height(), 480);
        EXPECT_EQ(resp.row_stride_bytes(), 0);
        ASSERT_GE(resp.image_data().size(), 3U);
        EXPECT_EQ(static_cast<uint8_t>(resp.image_data()[0]), 0xFFU);
        EXPECT_EQ(static_cast<uint8_t>(resp.image_data()[1]), 0xD8U);
        EXPECT_EQ(static_cast<uint8_t>(resp.image_data()[2]), 0xFFU);
    }

    // Quarter-turn rotation (90 deg) swaps output width and height (640x480 -> 480x640).
    physical_model_->SetGravity(-1.0F, 0.0F, 0.0F);  // Landscape (90 deg)
    {
        ::grpc::ServerContext ctx;
        GetScreenshotRequest req;
        req.set_display_id(0);
        req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
        Screenshot resp;
        ASSERT_TRUE(service_->GetScreenshot(&ctx, &req, &resp).ok());
        EXPECT_EQ(resp.rotation(), Rotation::ROTATION_DEG90);
        EXPECT_EQ(resp.width(), 480);
        EXPECT_EQ(resp.height(), 640);
    }

    // DISPLAY_FLAG_SECURE blocks screenshots with FAILED_PRECONDITION.
    {
        ::grpc::ServerContext create_ctx;
        CreateDisplayRequest create_req;
        create_req.set_display_id(2);
        create_req.set_width(400);
        create_req.set_height(300);
        create_req.set_density_dpi(240);
        create_req.add_flags(DisplayFlag::DISPLAY_FLAG_SECURE);
        Display created;
        ASSERT_TRUE(service_->CreateDisplay(&create_ctx, &create_req, &created).ok());

        ::grpc::ServerContext shot_ctx;
        GetScreenshotRequest shot_req;
        shot_req.set_display_id(2);
        Screenshot shot_resp;
        EXPECT_EQ(service_->GetScreenshot(&shot_ctx, &shot_req, &shot_resp).error_code(),
                  ::grpc::StatusCode::FAILED_PRECONDITION);
    }

    const auto stats = display::internal::GetScratchPoolStatsForTesting();
    EXPECT_EQ(stats.on_loan, 0U);
}

TEST_F(DisplayServiceImplTest, DisplayFlagsUseVirtualDisplayFlagBits) {
    // Displays created outside this service (e.g. by v1 setDisplayConfigurations) carry raw
    // DisplayManager.VIRTUAL_DISPLAY_FLAG_* bits.
    constexpr uint32_t kVirtualDisplayFlagPresentation = 1U << 1;
    constexpr uint32_t kVirtualDisplayFlagSecure = 1U << 2;
    constexpr uint32_t kVirtualDisplayFlagOwnContentOnly = 1U << 3;
    ASSERT_TRUE(
            multi_display_->CreateDisplay(1, 320, 240, 160, kVirtualDisplayFlagPresentation).ok());
    ASSERT_TRUE(multi_display_->CreateDisplay(2, 320, 240, 160, kVirtualDisplayFlagSecure).ok());

    {
        ::grpc::ServerContext ctx;
        ListDisplaysRequest req;
        ListDisplaysResponse resp;
        ASSERT_TRUE(service_->ListDisplays(&ctx, &req, &resp).ok());
        ASSERT_EQ(resp.displays_size(), 3);
        ASSERT_EQ(resp.displays(1).flags_size(), 1);
        EXPECT_EQ(resp.displays(1).flags(0), DisplayFlag::DISPLAY_FLAG_PRESENTATION);
        ASSERT_EQ(resp.displays(2).flags_size(), 1);
        EXPECT_EQ(resp.displays(2).flags(0), DisplayFlag::DISPLAY_FLAG_SECURE);
    }

    // Only the secure display refuses screenshots.
    {
        ::grpc::ServerContext ctx;
        GetScreenshotRequest req;
        req.set_display_id(1);
        Screenshot resp;
        const auto status = service_->GetScreenshot(&ctx, &req, &resp);
        EXPECT_TRUE(status.ok()) << status.error_message();
    }
    {
        ::grpc::ServerContext ctx;
        GetScreenshotRequest req;
        req.set_display_id(2);
        Screenshot resp;
        EXPECT_EQ(service_->GetScreenshot(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::FAILED_PRECONDITION);
    }

    // CreateDisplay passes the same bits on to the display.
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest req;
        req.set_display_id(3);
        req.set_width(320);
        req.set_height(240);
        req.set_density_dpi(160);
        req.add_flags(DisplayFlag::DISPLAY_FLAG_OWN_CONTENT_ONLY);
        Display resp;
        ASSERT_TRUE(service_->CreateDisplay(&ctx, &req, &resp).ok());
    }
    auto created = multi_display_->GetDisplay(3);
    ASSERT_TRUE(created.ok());
    auto disp = created->lock();
    ASSERT_NE(disp, nullptr);
    EXPECT_EQ(disp->Flags(), kVirtualDisplayFlagOwnContentOnly);
}

TEST_F(DisplayServiceImplTest, StreamScreenshotsEmitsInitialFrameRotationAndEndsOnDeletion) {
    CreateDisplayRequest create_req;
    create_req.set_display_id(1);
    create_req.set_width(100);
    create_req.set_height(50);
    create_req.set_density_dpi(240);
    Display created;
    {
        ::grpc::ServerContext ctx;
        ASSERT_TRUE(service_->CreateDisplay(&ctx, &create_req, &created).ok());
    }

    ::grpc::ClientContext stream_ctx;
    stream_ctx.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(5)));
    StreamScreenshotsRequest stream_req;
    stream_req.set_display_id(1);
    stream_req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);

    auto reader = stub_->StreamScreenshots(&stream_ctx, stream_req);

    // Immediate initial frame even when generator is idle.
    Screenshot frame1;
    ASSERT_TRUE(reader->Read(&frame1));
    EXPECT_EQ(frame1.width(), 100);
    EXPECT_EQ(frame1.height(), 50);
    EXPECT_EQ(frame1.rotation(), Rotation::ROTATION_DEG0);
    EXPECT_FLOAT_EQ(frame1.density_scale(), 240.0F / 160.0F);
    EXPECT_EQ(frame1.image_data().size(), 100U * 50U * 4U);

    // Rotation change on an idle display triggers a new frame with swapped dimensions.
    physical_model_->SetGravity(-1.0F, 0.0F, 0.0F);  // Landscape (ROTATION_DEG90)
    Screenshot frame2;
    ASSERT_TRUE(reader->Read(&frame2));
    EXPECT_EQ(frame2.rotation(), Rotation::ROTATION_DEG90);
    EXPECT_EQ(frame2.width(), 50);
    EXPECT_EQ(frame2.height(), 100);

    // Deleting display 1 terminates the stream with NOT_FOUND.
    {
        ::grpc::ClientContext del_ctx;
        DeleteDisplayRequest del_req;
        del_req.set_display_id(1);
        DeleteDisplayResponse del_resp;
        ASSERT_TRUE(stub_->DeleteDisplay(&del_ctx, del_req, &del_resp).ok());
    }
    Screenshot trailing;
    while (reader->Read(&trailing)) {
    }
    EXPECT_EQ(reader->Finish().error_code(), ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(DisplayServiceImplTest, StreamScreenshotsValidatesFpsAndSurvivesProducerReset) {
    // Invalid max_frames_per_second (<= 0 or NaN) fails immediately with INVALID_ARGUMENT.
    {
        ::grpc::ClientContext ctx;
        StreamScreenshotsRequest bad_req;
        bad_req.set_display_id(0);
        bad_req.set_max_frames_per_second(0.0F);
        auto reader = stub_->StreamScreenshots(&ctx, bad_req);
        Screenshot s;
        EXPECT_FALSE(reader->Read(&s));
        EXPECT_EQ(reader->Finish().error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
    }
    {
        ::grpc::ClientContext ctx;
        StreamScreenshotsRequest nan_req;
        nan_req.set_display_id(0);
        nan_req.set_max_frames_per_second(std::numeric_limits<float>::quiet_NaN());
        auto reader = stub_->StreamScreenshots(&ctx, nan_req);
        Screenshot s;
        EXPECT_FALSE(reader->Read(&s));
        EXPECT_EQ(reader->Finish().error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
    }

    // Stream with throttling and producer reset survives and continues delivering frames.
    auto disp = multi_display_->GetDisplay<ActiveFakePixmanDisplay>(multi_display_->GetDisplay(0));
    ASSERT_NE(disp, nullptr);
    StartFrames(0);

    ::grpc::ClientContext stream_ctx;
    stream_ctx.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(5)));
    StreamScreenshotsRequest req;
    req.set_display_id(0);
    req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
    req.set_max_frames_per_second(60.0F);

    auto reader = stub_->StreamScreenshots(&stream_ctx, req);
    Screenshot frame;
    ASSERT_TRUE(reader->Read(&frame));
    int64_t prev_frame_num = frame.frame_sequence_number();

    auto reset_notified = std::make_shared<absl::Notification>();
    disp->RunDuringNextResize([raw = disp.get(), reset_notified]() {
        raw->ResetProducer();
        reset_notified->Notify();
    });
    (void)disp->ResizeKeepAspectRatio(320, 240);

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(reader->Read(&frame));
        EXPECT_GE(frame.frame_sequence_number(), prev_frame_num);
        prev_frame_num = frame.frame_sequence_number();
    }
    EXPECT_TRUE(reset_notified->HasBeenNotified());

    stream_ctx.TryCancel();
    while (reader->Read(&frame)) {
    }
}

TEST_F(DisplayServiceImplTest, StreamScreenshotsConcurrentOverlappingCancelStress) {
    StartFrames(0);
    constexpr int kNumStreams = 40;
    std::vector<std::unique_ptr<::grpc::ClientContext>> contexts(kNumStreams);
    std::vector<absl::Notification> stream_started(kNumStreams);
    std::vector<std::thread> threads;
    threads.reserve(kNumStreams);

    for (int i = 0; i < kNumStreams; ++i) {
        contexts[i] = std::make_unique<::grpc::ClientContext>();
        contexts[i]->set_deadline(absl::ToChronoTime(absl::Now() + absl::Milliseconds(500)));
        threads.emplace_back([&, i]() {
            if (i > 0) {
                stream_started[i - 1].WaitForNotification();
            }
            StreamScreenshotsRequest req;
            req.set_display_id(0);
            req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);

            auto reader = stub_->StreamScreenshots(contexts[i].get(), req);
            stream_started[i].Notify();
            if (i > 0) {
                contexts[i - 1]->TryCancel();
            }
            Screenshot frame;
            int64_t last_seq = 0;
            while (reader->Read(&frame)) {
                EXPECT_GE(frame.frame_sequence_number(), last_seq);
                last_seq = frame.frame_sequence_number();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

TEST_F(DisplayServiceImplTest, ConcurrentUpdateDisplayAndGetDisplayNeverTear) {
    // Establish initial coherent Profile A before launching readers.
    UpdateDisplayRequest profile_a;
    profile_a.set_display_id(0);
    profile_a.set_brightness_percent(20.0F);
    profile_a.set_auto_brightness_enabled(false);
    profile_a.set_auto_rotation_enabled(false);
    profile_a.set_user_rotation(Rotation::ROTATION_DEG90);

    UpdateDisplayRequest profile_b;
    profile_b.set_display_id(0);
    profile_b.set_brightness_percent(80.0F);
    profile_b.set_auto_brightness_enabled(true);
    profile_b.set_auto_rotation_enabled(true);
    profile_b.set_user_rotation(Rotation::ROTATION_DEG270);

    {
        ::grpc::ServerContext init_ctx;
        Display init_resp;
        ASSERT_TRUE(service_->UpdateDisplay(&init_ctx, &profile_a, &init_resp).ok());
    }

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<int> torn_reads{0};

    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        constexpr int kUpdates = 2000;
        for (int i = 0; i < kUpdates; ++i) {
            ::grpc::ServerContext ctx;
            Display out;
            const UpdateDisplayRequest& req = (i % 2 == 0) ? profile_b : profile_a;
            (void)service_->UpdateDisplay(&ctx, &req, &out);
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
            GetDisplayRequest get_req;
            get_req.set_display_id(0);
            while (!done.load(std::memory_order_acquire)) {
                ::grpc::ServerContext ctx;
                Display snap;
                if (!service_->GetDisplay(&ctx, &get_req, &snap).ok()) {
                    continue;
                }
                const bool is_a =
                        (snap.brightness_percent() == 20.0F && !snap.auto_brightness_enabled() &&
                         !snap.auto_rotation_enabled() &&
                         snap.user_rotation() == Rotation::ROTATION_DEG90 &&
                         snap.rotation() == Rotation::ROTATION_DEG90);
                const bool is_b = (snap.brightness_percent() == 80.0F &&
                                   snap.auto_brightness_enabled() && snap.auto_rotation_enabled() &&
                                   snap.user_rotation() == Rotation::ROTATION_DEG270 &&
                                   snap.rotation() == Rotation::ROTATION_DEG0);
                if (!is_a && !is_b) {
                    torn_reads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    writer.join();
    for (auto& t : readers) {
        t.join();
    }

    EXPECT_EQ(torn_reads.load(), 0)
            << "GetDisplay observed torn multi-field intermediate state during UpdateDisplay";
}

TEST_F(DisplayServiceImplTest, UpdateDisplayRejectsDynamicResizingAndKeepsScreenshotStreamAlive) {
    CreateDisplayRequest create_req;
    create_req.set_display_id(1);
    create_req.set_width(800);
    create_req.set_height(600);
    create_req.set_density_dpi(240);
    Display created;
    {
        ::grpc::ServerContext ctx;
        ASSERT_TRUE(service_->CreateDisplay(&ctx, &create_req, &created).ok());
    }

    ::grpc::ClientContext stream_ctx;
    stream_ctx.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(5)));
    StreamScreenshotsRequest stream_req;
    stream_req.set_display_id(1);
    stream_req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
    auto reader = stub_->StreamScreenshots(&stream_ctx, stream_req);

    Screenshot initial_frame;
    ASSERT_TRUE(reader->Read(&initial_frame));
    EXPECT_EQ(initial_frame.width(), 800);
    EXPECT_EQ(initial_frame.height(), 600);

    // Active secondary displays cannot have width, height, or density_dpi dynamically resized.
    for (const auto& mutate_fn : std::vector<void (*)(UpdateDisplayRequest*)>{
             +[](UpdateDisplayRequest* r) { r->set_width(1280); },
             +[](UpdateDisplayRequest* r) { r->set_height(720); },
             +[](UpdateDisplayRequest* r) { r->set_density_dpi(320); },
         }) {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest resize_req;
        resize_req.set_display_id(1);
        mutate_fn(&resize_req);
        Display out;
        EXPECT_EQ(service_->UpdateDisplay(&ctx, &resize_req, &out).error_code(),
                  ::grpc::StatusCode::FAILED_PRECONDITION);
    }

    // StreamScreenshots on display 1 must still be alive after rejected resize attempts and after
    // valid non-geometry UpdateDisplay mutations.
    {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest rot_req;
        rot_req.set_display_id(1);
        rot_req.set_auto_rotation_enabled(false);
        rot_req.set_user_rotation(Rotation::ROTATION_DEG90);
        Display out;
        ASSERT_TRUE(service_->UpdateDisplay(&ctx, &rot_req, &out).ok());
    }

    Screenshot rotated_frame;
    ASSERT_TRUE(reader->Read(&rotated_frame));
    EXPECT_EQ(rotated_frame.rotation(), Rotation::ROTATION_DEG90);
    EXPECT_EQ(rotated_frame.width(), 600);
    EXPECT_EQ(rotated_frame.height(), 800);

    stream_ctx.TryCancel();
    while (reader->Read(&rotated_frame)) {
    }
}

TEST_F(DisplayServiceImplTest, ConcurrentCreateDisplayRespectsCapAndAutoAllocatesUniqueIds) {
    constexpr int kWorkers = 8;
    constexpr int kRounds = 25;

    for (int round = 0; round < kRounds; ++round) {
        multi_display_->Clear();

        // Explicit secondary IDs [1..8] racing against max_supported_display_count == 4 (primary 0
        // + at most 3 secondary displays).
        {
            std::atomic<bool> start{false};
            std::atomic<int> ok_count{0};
            std::atomic<int> exhausted_count{0};
            std::vector<std::thread> threads;
            threads.reserve(kWorkers);
            for (int w = 0; w < kWorkers; ++w) {
                threads.emplace_back([&, w]() {
                    while (!start.load(std::memory_order_acquire)) {
                    }
                    ::grpc::ServerContext ctx;
                    CreateDisplayRequest req;
                    req.set_display_id(w + 1);
                    req.set_width(320);
                    req.set_height(240);
                    req.set_density_dpi(160);
                    Display resp;
                    const ::grpc::Status status = service_->CreateDisplay(&ctx, &req, &resp);
                    if (status.ok()) {
                        ok_count.fetch_add(1, std::memory_order_relaxed);
                    } else if (status.error_code() == ::grpc::StatusCode::RESOURCE_EXHAUSTED) {
                        exhausted_count.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            start.store(true, std::memory_order_release);
            for (auto& t : threads) {
                t.join();
            }
            ASSERT_EQ(ok_count.load(), 3) << "Round " << round << " exceeded display cap";
            ASSERT_EQ(exhausted_count.load(), kWorkers - 3);
        }

        multi_display_->Clear();

        // Auto-allocated secondary IDs (display_id == 0) racing concurrently must allocate unique
        // IDs {1, 2, 3} without colliding on the same candidate ID.
        {
            std::atomic<bool> start{false};
            absl::Mutex ids_mu;
            std::unordered_set<int32_t> allocated_ids;
            std::atomic<int> exhausted_count{0};
            std::atomic<int> unexpected_errors{0};
            std::vector<std::thread> threads;
            threads.reserve(kWorkers);
            for (int w = 0; w < kWorkers; ++w) {
                threads.emplace_back([&]() {
                    while (!start.load(std::memory_order_acquire)) {
                    }
                    ::grpc::ServerContext ctx;
                    CreateDisplayRequest req;
                    req.set_width(320);
                    req.set_height(240);
                    req.set_density_dpi(160);
                    Display resp;
                    const ::grpc::Status status = service_->CreateDisplay(&ctx, &req, &resp);
                    if (status.ok()) {
                        const absl::MutexLock lock(ids_mu);
                        allocated_ids.insert(resp.display_id());
                    } else if (status.error_code() == ::grpc::StatusCode::RESOURCE_EXHAUSTED) {
                        exhausted_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        unexpected_errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            start.store(true, std::memory_order_release);
            for (auto& t : threads) {
                t.join();
            }
            ASSERT_EQ(unexpected_errors.load(), 0);
            ASSERT_EQ(exhausted_count.load(), kWorkers - 3);
            const std::unordered_set<int32_t> expected_ids = {1, 2, 3};
            ASSERT_EQ(allocated_ids, expected_ids);
        }
    }
}

class BuiltInFakePixmanDisplay final : public display::test::FakePixmanDisplay {
  public:
    BuiltInFakePixmanDisplay(async::EventLoop* loop, int id, uint32_t width, uint32_t height,
                             uint32_t dpi)
            : FakePixmanDisplay(
                      loop, id,
                      display::PixmanImagePtr(pixman_image_create_bits(
                              PIXMAN_a8r8g8b8, static_cast<int>(width), static_cast<int>(height),
                              nullptr, static_cast<int>(width) * 4)),
                      dpi, /*flags=*/0) {}

    QemuConsole* GetConsole() const override {
        return reinterpret_cast<QemuConsole*>(static_cast<uintptr_t>(0x1));
    }
};

class FoldableFakeMultiDisplay final : public display::IMultiDisplay {
  public:
    using DisplayId = display::DisplayId;

    explicit FoldableFakeMultiDisplay(async::EventLoop* loop) : IMultiDisplay(loop) {
        const absl::MutexLock lock(mu_);
        displays_[0] = std::make_shared<BuiltInFakePixmanDisplay>(loop, 0, 1840, 2208, 420);
        displays_[1] = std::make_shared<BuiltInFakePixmanDisplay>(loop, 1, 1080, 2092, 420);
        active_[0] = true;
        active_[1] = false;
    }

    absl::StatusOr<display::DisplayPtr> CreateDisplay(DisplayId id, uint32_t width, uint32_t height,
                                                      uint32_t dpi, uint32_t flags) override {
        std::shared_ptr<display::IDisplay> created;
        {
            const absl::MutexLock lock(mu_);
            if (displays_.contains(id)) {
                return absl::AlreadyExistsError(absl::StrFormat("Display %u already exists", id));
            }
            created = ActiveFakePixmanDisplay::CreateShared(loop_, static_cast<int>(id), 30,
                                                            static_cast<int>(width),
                                                            static_cast<int>(height), dpi, flags);
            displays_[id] = created;
            active_[id] = true;
        }
        SyncDisplayRotation(*created);
        FireEvent(display::DisplayEvent{display::DisplayEvent::AddedEvent{created}});
        return created;
    }

    bool IsEnabled() const override { return true; }

    absl::StatusOr<display::DisplayPtr> GetDisplay(DisplayId id) const override {
        const absl::MutexLock lock(mu_);
        const auto it = displays_.find(id);
        if (it == displays_.end()) {
            return absl::NotFoundError(absl::StrFormat("Display %u not found", id));
        }
        return it->second;
    }

    absl::Status EraseDisplay(DisplayId id) override {
        if (id == 0) {
            return absl::InvalidArgumentError("Cannot erase display 0");
        }
        std::shared_ptr<display::IDisplay> erased;
        {
            const absl::MutexLock lock(mu_);
            const auto it = displays_.find(id);
            if (it == displays_.end()) {
                return absl::NotFoundError(absl::StrFormat("Display %u not found", id));
            }
            erased = std::move(it->second);
            displays_.erase(it);
            active_.erase(id);
        }
        erased->Remove();
        FireEvent(display::DisplayEvent{display::DisplayEvent::DeletedEvent{id}});
        return absl::OkStatus();
    }

    std::vector<display::DisplayPtr> Displays() const override {
        const absl::MutexLock lock(mu_);
        std::vector<display::DisplayPtr> res;
        for (const auto& [id, disp] : displays_) {
            const auto active_it = active_.find(id);
            if (active_it != active_.end() && active_it->second) {
                res.push_back(disp);
            }
        }
        return res;
    }

    bool IsActive(DisplayId id) const override {
        const absl::MutexLock lock(mu_);
        const auto it = active_.find(id);
        return it != active_.end() && it->second;
    }

    absl::Status SetActive(DisplayId id, bool active) override {
        const absl::MutexLock lock(mu_);
        active_[id] = active;
        return absl::OkStatus();
    }

    void SetFolded(bool folded) override {
        const absl::MutexLock lock(mu_);
        folded_ = folded;
        active_[0] = !folded;
        active_[1] = folded;
    }

    bool IsFolded() const override {
        const absl::MutexLock lock(mu_);
        return folded_;
    }

    absl::Status Save(archive::IWriter& /*writer*/) const override { return absl::OkStatus(); }
    absl::Status Load(archive::IReader& /*reader*/) override { return absl::OkStatus(); }
    void Reset() override {}
    void SetDisplayMode(uint32_t /*mode*/, uint32_t /*width*/, uint32_t /*height*/,
                        uint32_t /*dpi*/, uint32_t /*guest_mode_id*/) override {}
    uint32_t GetDisplayMode() const override { return 0; }

  private:
    mutable absl::Mutex mu_;
    std::unordered_map<DisplayId, std::shared_ptr<display::IDisplay>> displays_
            ABSL_GUARDED_BY(mu_);
    std::unordered_map<DisplayId, bool> active_ ABSL_GUARDED_BY(mu_);
    bool folded_ ABSL_GUARDED_BY(mu_) = false;
};

TEST_F(DisplayServiceImplTest, FoldableInactiveAndBuiltInSecondaryDisplaysAreProtected) {
    auto foldable_hw = ::android::goldfish::FakeHardwareConfig::GetHwConfig();
    foldable_hw.hw_sensor_hinge = true;
    foldable_hw.hw_sensor_hinge_type = 1;
    foldable_hw.hw_sensor_hinge_count = 1;
    foldable_hw.hw_sensor_hinge_defaults = "180";
    foldable_hw.hw_sensor_hinge_ranges = "0-180";
    foldable_hw.hw_sensor_hinge_areas = "50-0";
    foldable_hw.hw_sensor_posture_list = "1,3";
    foldable_hw.hw_sensor_hinge_angles_posture_definitions = "0-30,150-180";

    sensors::PhysicalModel foldable_model(foldable_hw, virtual_clock_);
    ASSERT_TRUE(foldable_model.HasFoldableModel());
    FoldableFakeMultiDisplay foldable_displays(loop_.get());
    DisplayServiceImpl foldable_service(foldable_displays, &foldable_model,
                                        DisplayServiceImpl::Options{}, clock_);

    auto writer =
            foldable_service.StreamDisplaysForTesting<FakeWriteReactor<ListDisplaysResponse>>();
    ASSERT_EQ(writer->written_messages.size(), 1U);
    ASSERT_EQ(writer->written_messages.back().displays_size(), 1);
    EXPECT_EQ(writer->written_messages.back().displays(0).display_id(), 0);
    writer->OnWriteDone(true);

    // While unfolded, built-in cover display 1 is inactive: GetDisplay, UpdateDisplay, and
    // GetScreenshot must report NOT_FOUND consistent with ListDisplays.
    {
        ::grpc::ServerContext ctx;
        GetDisplayRequest req;
        req.set_display_id(1);
        Display out;
        EXPECT_EQ(foldable_service.GetDisplay(&ctx, &req, &out).error_code(),
                  ::grpc::StatusCode::NOT_FOUND);
    }
    {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest req;
        req.set_display_id(1);
        req.set_brightness_percent(50.0F);
        Display out;
        EXPECT_EQ(foldable_service.UpdateDisplay(&ctx, &req, &out).error_code(),
                  ::grpc::StatusCode::NOT_FOUND);
    }
    {
        ::grpc::ServerContext ctx;
        GetScreenshotRequest req;
        req.set_display_id(1);
        Screenshot out;
        EXPECT_EQ(foldable_service.GetScreenshot(&ctx, &req, &out).error_code(),
                  ::grpc::StatusCode::NOT_FOUND);
    }

    // Auto-allocating a virtual display must skip occupied built-in ID 1 and pick ID 2, and
    // explicit CreateDisplay(1) must return ALREADY_EXISTS.
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest explicit1;
        explicit1.set_display_id(1);
        explicit1.set_width(800);
        explicit1.set_height(600);
        explicit1.set_density_dpi(240);
        Display out;
        EXPECT_EQ(foldable_service.CreateDisplay(&ctx, &explicit1, &out).error_code(),
                  ::grpc::StatusCode::ALREADY_EXISTS);
    }
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest auto_req;
        auto_req.set_width(800);
        auto_req.set_height(600);
        auto_req.set_density_dpi(240);
        Display out;
        ASSERT_TRUE(foldable_service.CreateDisplay(&ctx, &auto_req, &out).ok());
        EXPECT_EQ(out.display_id(), 2);
        writer->OnWriteDone(true);
    }

    // Built-in secondary display 1 cannot be deleted even when inactive or active.
    {
        ::grpc::ServerContext ctx;
        DeleteDisplayRequest del1;
        del1.set_display_id(1);
        DeleteDisplayResponse out;
        EXPECT_EQ(foldable_service.DeleteDisplay(&ctx, &del1, &out).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }

    // Folding the device (posture kClosed) switches active display from 0 to 1 and notifies
    // StreamDisplays subscribers.
    foldable_model.SetTargetPosture(static_cast<float>(sensors::FoldablePostures::kClosed),
                                    ::PhysicalInterpolation::kStep);
    EXPECT_TRUE(foldable_displays.IsFolded());
    ASSERT_GE(writer->written_messages.size(), 2U);
    EXPECT_EQ(writer->written_messages.back().displays(0).display_id(), 1);
    EXPECT_EQ(writer->written_messages.back().displays(1).display_id(), 2);

    // Deleting built-in secondary display 1 while active also fails with INVALID_ARGUMENT.
    {
        ::grpc::ServerContext ctx;
        DeleteDisplayRequest del1;
        del1.set_display_id(1);
        DeleteDisplayResponse out;
        EXPECT_EQ(foldable_service.DeleteDisplay(&ctx, &del1, &out).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }
}

TEST_F(DisplayServiceImplTest, SmoothPhysicalModelRotationUpdatesDisplayAndStreamDisplays) {
    physical_model_->SetCurrentTime(0);
    auto writer = service_->StreamDisplaysForTesting<FakeWriteReactor<ListDisplaysResponse>>();
    ASSERT_EQ(writer->written_messages.size(), 1U);
    EXPECT_EQ(writer->written_messages.back().displays(0).rotation(), Rotation::ROTATION_DEG0);
    writer->OnWriteDone(true);

    // Smoothly interpolate rotation by 90 degrees around Z; at t=0 the body is still at 0 degrees,
    // and only reaches 90 degrees when SetCurrentTime stabilizes the inertial model.
    physical_model_->SetTargetRotation(glm::vec3(0.0F, 0.0F, 90.0F),
                                       ::PhysicalInterpolation::kSmooth);
    physical_model_->SetCurrentTime(2'000'000'000LL);

    const Rotation expected_rotation = ToProtoRotation(
            display::ToImageRotation(physical_model_->GetDeviceRotation().rotation));
    ASSERT_NE(expected_rotation, Rotation::ROTATION_DEG0);

    EXPECT_GE(writer->written_messages.size(), 2U);
    EXPECT_EQ(writer->written_messages.back().displays(0).rotation(), expected_rotation);

    ::grpc::ServerContext get_ctx;
    GetDisplayRequest get_req;
    get_req.set_display_id(0);
    Display fetched;
    ASSERT_TRUE(service_->GetDisplay(&get_ctx, &get_req, &fetched).ok());
    EXPECT_EQ(fetched.rotation(), expected_rotation);
}

TEST_F(DisplayServiceImplTest,
       StreamScreenshotsFpsThrottlingUsesInjectedSteadyClockAndCoalescesBursts) {
    auto disp = multi_display_->GetDisplay<ActiveFakePixmanDisplay>(multi_display_->GetDisplay(0));
    ASSERT_NE(disp, nullptr);

    // 0.1 FPS (10-second interval): advancing the injected SteadyClock by 11 seconds must
    // immediately unblock the next frame without sleeping 10 wall-clock seconds.
    {
        ::grpc::ClientContext stream_ctx;
        stream_ctx.set_deadline(absl::ToChronoTime(absl::Now() + absl::Milliseconds(1500)));
        StreamScreenshotsRequest req;
        req.set_display_id(0);
        req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
        req.set_max_frames_per_second(0.1F);

        auto reader = stub_->StreamScreenshots(&stream_ctx, req);
        Screenshot frame1;
        ASSERT_TRUE(reader->Read(&frame1));

        clock_.Advance(absl::Seconds(11));
        disp->UpdateSurface(0, 0, 640, 480);

        Screenshot frame2;
        ASSERT_TRUE(reader->Read(&frame2))
                << "StreamScreenshots ignored injected SteadyClock and stayed throttled";
        EXPECT_GT(frame2.frame_sequence_number(), frame1.frame_sequence_number());
        EXPECT_EQ(frame2.presentation_time_nanos(), 21'000'000'000LL);

        stream_ctx.TryCancel();
        while (reader->Read(&frame2)) {
        }
    }

    // Burst coalescing: a rapid burst of 5 frames within a 50ms throttle window coalesces onto the
    // latest frame emitted when the throttle timer fires.
    {
        ::grpc::ClientContext stream_ctx;
        stream_ctx.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(5)));
        StreamScreenshotsRequest req;
        req.set_display_id(0);
        req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
        req.set_max_frames_per_second(20.0F);

        auto reader = stub_->StreamScreenshots(&stream_ctx, req);
        Screenshot initial;
        ASSERT_TRUE(reader->Read(&initial));
        const int64_t initial_seq = initial.frame_sequence_number();

        for (int b = 0; b < 5; ++b) {
            disp->UpdateSurface(0, 0, 640, 480);
            ASSERT_TRUE(disp->WaitForFreshFrame(absl::Now() + absl::Seconds(1)).ok());
        }
        clock_.Advance(absl::Milliseconds(60));

        Screenshot coalesced;
        ASSERT_TRUE(reader->Read(&coalesced));
        EXPECT_GT(coalesced.frame_sequence_number(), initial_seq);

        stream_ctx.TryCancel();
        while (reader->Read(&coalesced)) {
        }
    }
}

TEST_F(DisplayServiceImplTest,
       EncodeFrameUsesFrameRotationSnapshotAndBlankedDisplayFailsGetScreenshot) {
    auto disp = multi_display_->GetDisplay<ActiveFakePixmanDisplay>(multi_display_->GetDisplay(0));
    ASSERT_NE(disp, nullptr);

    // Capture a frame at ROTATION_DEG0, then mutate display rotation to ROTATION_DEG90 before
    // encoding: EncodeFrame must honor frame.rotation() (ROTATION_DEG0, 640x480), not live
    // display.GetRotation() (ROTATION_DEG90, 480x640).
    auto captured_frame = disp->WaitForFreshFrame(absl::Now() + absl::Seconds(1));
    ASSERT_TRUE(captured_frame.ok()) << captured_frame.status();
    ASSERT_EQ((*captured_frame)->rotation(), display::ImageRotation::kRotation0);

    disp->SetRotation(display::ImageRotation::kRotation90);
    ASSERT_EQ(disp->GetRotation(), display::ImageRotation::kRotation90);

    ScreenshotCapturer capturer(420, clock_);
    Screenshot encoded;
    ASSERT_TRUE(capturer.EncodeFrame(*disp, **captured_frame,
                                     ImageFormat::IMAGE_FORMAT_RAW_RGBA8888, &encoded)
                        .ok());
    EXPECT_EQ(encoded.rotation(), Rotation::ROTATION_DEG0);
    EXPECT_EQ(encoded.width(), 640);
    EXPECT_EQ(encoded.height(), 480);

    // Mid-ReadInto producer reset returns UNAVAILABLE from EncodeFrame.
    disp->ResetProducer();
    Screenshot reset_encoded;
    EXPECT_TRUE(absl::IsUnavailable(capturer.EncodeFrame(
            *disp, **captured_frame, ImageFormat::IMAGE_FORMAT_RAW_RGBA8888, &reset_encoded)));

    // Blanked display (no guest surface content -> blank frame) must fail GetScreenshot with
    // FAILED_PRECONDITION rather than returning an opaque black image as OK.
    disp->UpdateSourceImage(nullptr);
    disp->SetDimensions(640, 480);
    disp->ResetProducer();

    ::grpc::ServerContext shot_ctx;
    GetScreenshotRequest shot_req;
    shot_req.set_display_id(0);
    Screenshot shot_resp;
    EXPECT_EQ(service_->GetScreenshot(&shot_ctx, &shot_req, &shot_resp).error_code(),
              ::grpc::StatusCode::FAILED_PRECONDITION);
}

class DelegatedScreenshotService final
        : public DisplayService::WithCallbackMethod_StreamScreenshots<DisplayService::Service> {
  public:
    DelegatedScreenshotService(std::shared_ptr<display::IDisplay> disp,
                               std::shared_ptr<ScreenshotCapturer> capturer)
            : disp_(std::move(disp)), capturer_(std::move(capturer)) {}

    ::grpc::ServerWriteReactor<Screenshot>* StreamScreenshots(
            ::grpc::CallbackServerContext* /*context*/,
            const StreamScreenshotsRequest* request) override {
        return capturer_->StreamScreenshots(disp_, *request);
    }

    void DestroyCapturer() { capturer_.reset(); }

  private:
    std::shared_ptr<display::IDisplay> disp_;
    std::shared_ptr<ScreenshotCapturer> capturer_;
};

TEST_F(DisplayServiceImplTest, ScreenshotCapturerDestructionWhileStreamActiveIsSafe) {
    auto disp = multi_display_->GetDisplay<ActiveFakePixmanDisplay>(multi_display_->GetDisplay(0));
    ASSERT_NE(disp, nullptr);

    auto capturer = std::make_shared<ScreenshotCapturer>(420, clock_);
    DelegatedScreenshotService delegated_service(disp, std::move(capturer));

    ::grpc::ServerBuilder builder;
    builder.RegisterService(&delegated_service);
    auto local_server = builder.BuildAndStart();
    ASSERT_NE(local_server, nullptr);
    auto local_channel = local_server->InProcessChannel(::grpc::ChannelArguments());
    auto local_stub = DisplayService::NewStub(local_channel);

    ::grpc::ClientContext stream_ctx;
    stream_ctx.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(5)));
    StreamScreenshotsRequest req;
    req.set_display_id(0);
    req.set_format(ImageFormat::IMAGE_FORMAT_RAW_RGBA8888);
    req.set_max_frames_per_second(5.0F);

    auto reader = local_stub->StreamScreenshots(&stream_ctx, req);
    Screenshot frame;
    ASSERT_TRUE(reader->Read(&frame));

    // Schedule a throttle timer and start continuous frame generation before destroying capturer.
    disp->UpdateSurface(0, 0, 640, 480);
    StartFrames(0);

    // Destroying ScreenshotCapturer while the gRPC stream reactor is still active must cleanly
    // terminate/detach the stream without UAF on worker_loop_ or capturer.
    delegated_service.DestroyCapturer();
    disp->UpdateSurface(0, 0, 640, 480);

    stream_ctx.TryCancel();
    while (reader->Read(&frame)) {
    }
    EXPECT_FALSE(reader->Finish().ok());

    StopFrames();
    local_server->Shutdown(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
    local_server->Wait();
}

TEST_F(DisplayServiceImplTest, ReadOnlyModeRejectsMutationsWithPermissionDenied) {
    DisplayServiceImpl::Options ro_opts;
    ro_opts.read_only = true;
    StartServer(ro_opts);

    {
        ::grpc::ServerContext ctx;
        ListDisplaysRequest req;
        ListDisplaysResponse resp;
        EXPECT_TRUE(service_->ListDisplays(&ctx, &req, &resp).ok());
    }
    {
        ::grpc::ServerContext ctx;
        GetScreenshotRequest req;
        req.set_display_id(0);
        Screenshot resp;
        EXPECT_TRUE(service_->GetScreenshot(&ctx, &req, &resp).ok());
    }
    {
        ::grpc::ServerContext ctx;
        UpdateDisplayRequest req;
        req.set_display_id(0);
        req.set_brightness_percent(50.0F);
        Display resp;
        EXPECT_EQ(service_->UpdateDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::PERMISSION_DENIED);
    }
    {
        ::grpc::ServerContext ctx;
        CreateDisplayRequest req;
        req.set_width(800);
        req.set_height(600);
        req.set_density_dpi(240);
        Display resp;
        EXPECT_EQ(service_->CreateDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::PERMISSION_DENIED);
    }
    {
        ::grpc::ServerContext ctx;
        DeleteDisplayRequest req;
        req.set_display_id(1);
        DeleteDisplayResponse resp;
        EXPECT_EQ(service_->DeleteDisplay(&ctx, &req, &resp).error_code(),
                  ::grpc::StatusCode::PERMISSION_DENIED);
    }
}

}  // namespace
}  // namespace goldfish::grpc::v2
