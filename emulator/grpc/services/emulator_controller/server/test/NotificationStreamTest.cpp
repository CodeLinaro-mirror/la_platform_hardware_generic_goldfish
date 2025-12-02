// Copyright (C) 2023 The Android Open Source Project
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
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "GrpcServiceTest.h"
#include "absl/log/log.h"

#include "android/emulation/control/EmulatorService.h"
#include "android/emulation/control/NotificationStream.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/test_fake_connector_registry.h"
#include "goldfish/display/test/FakeMultiDisplay.h"
#include "goldfish/display/test/FakePixmanDisplay.h"

namespace android::emulation::control {

using ::goldfish::devices::ConnectorRegistry;
using ::goldfish::devices::FakeConnectorRegistry;
using ::goldfish::display::IMultiDisplay;
using ::goldfish::display::test::ActiveFakePixmanDisplay;
using ::goldfish::display::test::FakeMultiDisplay;
using ::google::protobuf::Empty;
using google::protobuf::TextFormat;
using grpc::ServerContext;
using grpc::Status;

using namespace std::chrono_literals;

// Logic and data behind the server's behavior.
class NotificationStreamImpl final
        : public EmulatorController::WithCallbackMethod_streamNotification<
                  EmulatorController::Service> {
  public:
    NotificationStreamImpl(IMultiDisplay* multidisplay, ConnectorRegistry* connectorRegistry)
            : mNotificationStream(NotificationStream::create(multidisplay, connectorRegistry)) {}

    ::grpc::ServerWriteReactor<Notification>* streamNotification(
            ::grpc::CallbackServerContext* context,
            const ::google::protobuf::Empty* request) override {
        return mNotificationStream->notificationStream();
    }

  private:
    std::shared_ptr<NotificationStream> mNotificationStream;
};

class NotificationServiceTest : public GrcpServiceTest {
  protected:
    void SetUp() override {
        mLoop = ::goldfish::async::ThreadedEventLoop::create(
                ::goldfish::async::LibuvEventLoop::create());

        // Clear all displays except the default one before each test
        mMultiDisplay = std::make_unique<FakeMultiDisplay>(mLoop.get());
        mNotificationServiceImpl =
                std::make_unique<NotificationStreamImpl>(mMultiDisplay.get(), &mRegistry);
        GrcpServiceTest::SetUp();
    }

    EmulatorController::Service* getService() override { return mNotificationServiceImpl.get(); }

  protected:
    std::unique_ptr<::goldfish::async::EventLoop> mLoop;
    std::unique_ptr<FakeMultiDisplay> mMultiDisplay;
    FakeConnectorRegistry mRegistry;
    std::unique_ptr<NotificationStreamImpl> mNotificationServiceImpl;
};

TEST_F(NotificationServiceTest, InitialDisplayConfiguration) {
    Empty request;
    Notification reply;
    auto context = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // The first notification should be the current display configuration.
    // The FakeMultiDisplay starts with one display.
    if (!reader->Read(&reply)) {
        auto status = reader->Finish();
        LOG(ERROR) << "Failed to read first notification: " << status.error_code()
                   << " msg: " << status.error_message() << ", details: " << status.error_details();
        // ASSERT_TRUE(reader->Read(&reply));
    };
    ASSERT_EQ(reply.type_case(), Notification::TypeCase::kDisplayConfigurationsChangedNotification);
    EXPECT_EQ(1, reply.displayconfigurationschangednotification()
                         .displayconfigurations()
                         .displays_size());

    // Now close the stream..
    context->TryCancel();
    EXPECT_FALSE(reader->Read(&reply));
    auto status = reader->Finish();
    EXPECT_EQ(grpc::StatusCode::CANCELLED, status.error_code());
}

// b/448934377
TEST_F(NotificationServiceTest, DISABLED_BootCompletedNotification) {
    Empty request;
    Notification reply;
    auto context = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // Read the initial display configuration.
    ASSERT_TRUE(reader->Read(&reply));

    // Trigger a boot completed event.
    mRegistry.guestDevice()->sendBootCompleted();

    // We should receive a boot completed notification.
    ASSERT_TRUE(reader->Read(&reply));
    ASSERT_EQ(reply.type_case(), Notification::TypeCase::kBooted);
    EXPECT_EQ(1234, reply.booted().time());

    // Now close the stream..
    context->TryCancel();
    EXPECT_FALSE(reader->Read(&reply));
    auto status = reader->Finish();
    EXPECT_EQ(grpc::StatusCode::CANCELLED, status.error_code());
}

TEST_F(NotificationServiceTest, DisplayConfigurationChange) {
    Empty request;
    Notification reply;
    auto context = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // Read the initial display configuration.
    ASSERT_TRUE(reader->Read(&reply));

    // Add a new display.
    ASSERT_TRUE(mMultiDisplay->createDisplay(1, 1080, 1920).ok());

    // We should receive a new display configuration.
    ASSERT_TRUE(reader->Read(&reply));
    ASSERT_EQ(reply.type_case(), Notification::TypeCase::kDisplayConfigurationsChangedNotification);
    EXPECT_EQ(2, reply.displayconfigurationschangednotification()
                         .displayconfigurations()
                         .displays_size());

    // Now close the stream..
    context->TryCancel();
    EXPECT_FALSE(reader->Read(&reply));
    auto status = reader->Finish();
    EXPECT_EQ(grpc::StatusCode::CANCELLED, status.error_code());
}

TEST_F(NotificationServiceTest, StreamCancellation) {
    Empty request;
    Notification reply;
    auto context = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // Read the initial display configuration.
    ASSERT_TRUE(reader->Read(&reply));

    // Cancel the stream.
    context->TryCancel();
    EXPECT_FALSE(reader->Read(&reply));
    auto status = reader->Finish();
    EXPECT_EQ(grpc::StatusCode::CANCELLED, status.error_code());
}

// b/448934377
TEST_F(NotificationServiceTest, DISABLED_MultipleClients) {
    Empty request;
    Notification reply1, reply2;
    auto context1 = getContextWithTimeout();
    auto context2 = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader1(
            mStub->streamNotification(context1.get(), request));
    std::unique_ptr<grpc::ClientReader<Notification>> reader2(
            mStub->streamNotification(context2.get(), request));

    // Read the initial display configuration for both clients.
    ASSERT_TRUE(reader1->Read(&reply1));
    ASSERT_TRUE(reader2->Read(&reply2));

    // Trigger a boot completed event.
    mRegistry.guestDevice()->sendBootCompleted();

    // Both clients should receive the boot completed notification.
    ASSERT_TRUE(reader1->Read(&reply1));
    ASSERT_TRUE(reader2->Read(&reply2));
    ASSERT_EQ(reply1.type_case(), Notification::TypeCase::kBooted);
    ASSERT_EQ(reply2.type_case(), Notification::TypeCase::kBooted);

    // Cancel the streams.
    context1->TryCancel();
    context2->TryCancel();
    EXPECT_FALSE(reader1->Read(&reply1));
    EXPECT_FALSE(reader2->Read(&reply2));
}

// b/448934377
TEST_F(NotificationServiceTest, DISABLED_CombinedEvents) {
    Empty request;
    Notification reply;
    auto context = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // Read the initial display configuration.
    ASSERT_TRUE(reader->Read(&reply));

    // Trigger a boot completed event and add a new display.
    mRegistry.guestDevice()->sendBootCompleted();
    ASSERT_TRUE(mMultiDisplay->createDisplay(1, 1080, 1920).ok());

    // Read all available notifications.
    std::vector<Notification> notifications;
    while (reader->Read(&reply)) {
        notifications.push_back(reply);
        if (notifications.size() == 2) {
            break;
        }
    }

    // Check for the display configuration and boot completed notifications.
    bool found_display_config = false;
    bool found_boot_completed = false;
    for (const auto& notif : notifications) {
        if (notif.type_case() ==
            Notification::TypeCase::kDisplayConfigurationsChangedNotification) {
            found_display_config = true;
            EXPECT_EQ(2, notif.displayconfigurationschangednotification()
                                 .displayconfigurations()
                                 .displays_size());
        } else if (notif.type_case() == Notification::TypeCase::kBooted) {
            found_boot_completed = true;
            EXPECT_EQ(1234, notif.booted().time());
        }
    }

    EXPECT_TRUE(found_display_config);
    EXPECT_TRUE(found_boot_completed);

    // Now close the stream..
    context->TryCancel();
    EXPECT_FALSE(reader->Read(&reply));
    auto status = reader->Finish();
    EXPECT_EQ(grpc::StatusCode::CANCELLED, status.error_code());
}

// b/448934377
TEST_F(NotificationServiceTest, DISABLED_DuplicateBootCompletedNotification) {
    Empty request;
    Notification reply;

    // Note we are going to block and wait, so let's not have a 10s timeout.
    auto context = getContextWithTimeout(500ms);
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // Read the initial display configuration.
    ASSERT_TRUE(reader->Read(&reply));

    // Trigger a boot completed event.
    mRegistry.guestDevice()->sendBootCompleted();

    // We should receive a boot completed notification.
    ASSERT_TRUE(reader->Read(&reply));
    ASSERT_EQ(reply.type_case(), Notification::TypeCase::kBooted);
    EXPECT_EQ(1234, reply.booted().time());

    // Trigger another boot completed event.
    mRegistry.guestDevice()->sendBootCompleted();

    // We should NOT receive another boot completed notification.
    // so this should timeout!
    EXPECT_FALSE(reader->Read(&reply));

    // Now close the stream, since we timed out we are likely cancelled already.
    context->TryCancel();
    auto status = reader->Finish();
}

TEST_F(NotificationServiceTest, DISABLED_DisplayResolutionChange) {
    Empty request;
    Notification reply;
    auto context = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // Read the initial display configuration.
    ASSERT_TRUE(reader->Read(&reply));

    // Get the default display and resize it.
    auto display = mMultiDisplay->getDisplay<ActiveFakePixmanDisplay>(
            mMultiDisplay->defaultDisplay());
    display->start();
    display->resize(1080, 1920);

    // Generate some frames to make sure the display is updated.
    ASSERT_TRUE(display->waitForFramesWithTimeout(5, absl::Milliseconds(1000)));

    // We should receive a new display configuration.
    ASSERT_TRUE(reader->Read(&reply));
    ASSERT_EQ(reply.type_case(), Notification::TypeCase::kDisplayConfigurationsChangedNotification);
    const auto& displayConfig =
            reply.displayconfigurationschangednotification().displayconfigurations();
    EXPECT_EQ(displayConfig.displays_size(), 1);
    EXPECT_EQ(displayConfig.displays(0).width(), 1080);
    EXPECT_EQ(displayConfig.displays(0).height(), 1920);

    // Now close the stream..
    context->TryCancel();
    EXPECT_FALSE(reader->Read(&reply));
    auto status = reader->Finish();
    EXPECT_EQ(grpc::StatusCode::CANCELLED, status.error_code());
}

TEST_F(NotificationServiceTest, DISABLED_RemoveDisplay) {
    Empty request;
    Notification reply;
    auto context = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // Read the initial display configuration.
    ASSERT_TRUE(reader->Read(&reply));

    // Add a new display.
    ASSERT_TRUE(mMultiDisplay->createDisplay(1, 1080, 1920).ok());

    // We should receive a new display configuration.
    ASSERT_TRUE(reader->Read(&reply));
    ASSERT_EQ(reply.type_case(), Notification::TypeCase::kDisplayConfigurationsChangedNotification);
    EXPECT_EQ(2, reply.displayconfigurationschangednotification()
                         .displayconfigurations()
                         .displays_size());

    // Remove the display.
    ASSERT_TRUE(mMultiDisplay->eraseDisplay(1).ok());

    // We should receive another display configuration.
    ASSERT_TRUE(reader->Read(&reply));
    ASSERT_EQ(reply.type_case(), Notification::TypeCase::kDisplayConfigurationsChangedNotification);
    EXPECT_EQ(1, reply.displayconfigurationschangednotification()
                         .displayconfigurations()
                         .displays_size());

    // Now close the stream..
    context->TryCancel();
    EXPECT_FALSE(reader->Read(&reply));
    auto status = reader->Finish();
    EXPECT_EQ(grpc::StatusCode::CANCELLED, status.error_code());
}

TEST_F(NotificationServiceTest, DISABLED_RemoveMultipleDisplays) {
    Empty request;
    Notification reply;
    auto context = getContextWithTimeout();
    std::unique_ptr<grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // Read the initial display configuration.
    ASSERT_TRUE(reader->Read(&reply));

    // Add two new displays.
    ASSERT_TRUE(mMultiDisplay->createDisplay(1, 1080, 1920).ok());
    ASSERT_TRUE(reader->Read(&reply));
    ASSERT_TRUE(mMultiDisplay->createDisplay(2, 1920, 1080).ok());
    ASSERT_TRUE(reader->Read(&reply));

    // We should have 3 displays.
    ASSERT_EQ(3, reply.displayconfigurationschangednotification()
                         .displayconfigurations()
                         .displays_size());

    // Remove the displays.
    ASSERT_TRUE(mMultiDisplay->eraseDisplay(1).ok());
    ASSERT_TRUE(reader->Read(&reply));
    ASSERT_TRUE(mMultiDisplay->eraseDisplay(2).ok());
    ASSERT_TRUE(reader->Read(&reply));

    // We should have 1 display.
    ASSERT_EQ(1, reply.displayconfigurationschangednotification()
                         .displayconfigurations()
                         .displays_size());

    // Now close the stream..
    context->TryCancel();
    EXPECT_FALSE(reader->Read(&reply));
    auto status = reader->Finish();
    EXPECT_EQ(grpc::StatusCode::CANCELLED, status.error_code());
}

}  // namespace android::emulation::control
