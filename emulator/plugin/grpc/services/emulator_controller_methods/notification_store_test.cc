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

#include "notification_store.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "notification_stream_writer.h"
#include "test/GrpcServiceTest.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::avd_universe::grpc::GrpcNotification;
using ::goldfish::avd_universe::grpc::GrpcNotificationEventSource;
using ::google::protobuf::Empty;
using ::testing::UnorderedElementsAre;

class NotificationStoreTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mNotificationStore = std::make_unique<NotificationStore>(&mSource);
    }

    GrpcNotificationEventSource mSource;
    std::unique_ptr<NotificationStore> mNotificationStore;
};

TEST_F(NotificationStoreTest, StoreStickyNotifications) {
    GrpcNotification bootEvent;
    bootEvent.mutable_booted()->set_time(1000);
    mSource.FireEvent(bootEvent);

    GrpcNotification postureEvent;
    postureEvent.mutable_posture()->set_value(Posture::POSTURE_OPENED);
    mSource.FireEvent(postureEvent);

    auto latest = mNotificationStore->GetLatest();
    EXPECT_EQ(latest.size(), 2);

    bool foundBoot = false;
    bool foundPosture = false;
    for (const auto& n : latest) {
        if (n.has_booted()) {
            foundBoot = true;
            EXPECT_EQ(n.booted().time(), 1000);
        } else if (n.has_posture()) {
            foundPosture = true;
            EXPECT_EQ(n.posture().value(), Posture::POSTURE_OPENED);
        }
    }
    EXPECT_TRUE(foundBoot);
    EXPECT_TRUE(foundPosture);
}

TEST_F(NotificationStoreTest, NonStickyNotStored) {
    GrpcNotification brightnessEvent;
    brightnessEvent.mutable_brightness()->set_value(50);
    mSource.FireEvent(brightnessEvent);

    auto latest = mNotificationStore->GetLatest();
    EXPECT_EQ(latest.size(), 0);
}

class MockEmulatorControllerService
        : public EmulatorController::WithCallbackMethod_streamNotification<
                  EmulatorController::Service> {
  public:
    MockEmulatorControllerService(GrpcNotificationEventSource* source, NotificationStore* store)
        : mSource(source), mStore(store) {}

    ::grpc::ServerWriteReactor<Notification>* streamNotification(
            ::grpc::CallbackServerContext* /*context*/, const Empty* /*request*/) override {
        return new NotificationStreamWriter(mSource, mStore);
    }

  private:
    GrpcNotificationEventSource* mSource;
    NotificationStore* mStore;
};

class NotificationReplayTest : public GrcpServiceTest {
  protected:
    void SetUp() override {
        mNotificationStore = std::make_unique<NotificationStore>(&mSource);
        mService = std::make_unique<MockEmulatorControllerService>(&mSource,
                                                                   mNotificationStore.get());
        GrcpServiceTest::SetUp();
    }

    EmulatorController::Service* getService() override { return mService.get(); }

    GrpcNotificationEventSource mSource;
    std::unique_ptr<NotificationStore> mNotificationStore;
    std::unique_ptr<MockEmulatorControllerService> mService;
};

TEST_F(NotificationReplayTest, ReplaysStoredNotifications) {
    // 1. Fire some notifications BEFORE the client connects.
    GrpcNotification bootEvent;
    bootEvent.mutable_booted()->set_time(1234);
    mSource.FireEvent(bootEvent);

    GrpcNotification postureEvent;
    postureEvent.mutable_posture()->set_value(Posture::POSTURE_CLOSED);
    mSource.FireEvent(postureEvent);

    // 2. Connect a client and start streaming.
    Empty request;
    auto context = getContextWithTimeout();
    std::unique_ptr<::grpc::ClientReader<Notification>> reader(
            mStub->streamNotification(context.get(), request));

    // 3. Verify the client immediately receives the stored notifications.
    Notification n1, n2;
    EXPECT_TRUE(reader->Read(&n1));
    EXPECT_TRUE(reader->Read(&n2));

    // The order might depend on the map iteration, so we check both.
    std::vector<Notification> received = {n1, n2};
    bool foundBoot = false;
    bool foundPosture = false;
    for (const auto& n : received) {
        if (n.has_booted()) {
            foundBoot = true;
            EXPECT_EQ(n.booted().time(), 1234);
        } else if (n.has_posture()) {
            foundPosture = true;
            EXPECT_EQ(n.posture().value(), Posture::POSTURE_CLOSED);
        }
    }
    EXPECT_TRUE(foundBoot);
    EXPECT_TRUE(foundPosture);

    // 4. Fire a new event and verify the client receives it.
    GrpcNotification cameraEvent;
    cameraEvent.mutable_cameranotification()->set_active(true);
    mSource.FireEvent(cameraEvent);

    Notification n3;
    EXPECT_TRUE(reader->Read(&n3));
    EXPECT_TRUE(n3.has_cameranotification());
    EXPECT_EQ(n3.cameranotification().active(), true);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
