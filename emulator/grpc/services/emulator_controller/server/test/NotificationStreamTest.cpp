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

}  // namespace android::emulation::control
