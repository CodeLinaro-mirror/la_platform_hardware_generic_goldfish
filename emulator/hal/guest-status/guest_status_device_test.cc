// Copyright (C) 2017 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "goldfish/devices/guest_status/guest_status_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include "absl/strings/numbers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "android/base/testing/TestSystem.h"
#include "goldfish/devices/test_connector_registry.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"

namespace goldfish::devices::guest_status {

using android::base::TestSystem;
using async::testing::TestEventLoop;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;

using namespace std::literals::string_view_literals;

namespace {
typedef void QEMUResetHandler(void* opaque);

static QEMUResetHandler* sResetHandler;
static void* sOpaque;

void qemu_register_reset(QEMUResetHandler* func, void* opaque) {
    sResetHandler = func;
    sOpaque = opaque;
}

void qemu_unregister_reset(QEMUResetHandler* func, void* opaque) {}

struct MockNotificationSource : public avd_universe::grpc::GrpcNotificationEventSource {
    MOCK_METHOD(void, FireEvent, (const avd_universe::grpc::GrpcNotification& event), ());
};

}  // namespace

class GuestStatusDeviceTest : public ::testing::Test {
    void SetUp() override {
        mClientLoop = TestEventLoop::Create();
        mQemuLoop = TestEventLoop::Create();

        IGuestStatusDevice::RegisterDevice(&mGuestStatus, &mNotificationSource, &registry,
                                           {qemu_register_reset, qemu_unregister_reset},
                                           mClientLoop.get(), mQemuLoop.get(), 0);
        device = registry.ConstructHalDevice<IGuestStatusDevice>();
        test_socket = registry.HalSocket();
        clear();
    }

  public:
    void receive(const std::string_view msg) {
        char sizeBuf[sizeof(uint32_t)];
        absl::little_endian::Store32(sizeBuf, msg.size());
        device->OnReceive(std::string_view(sizeBuf, sizeof(sizeBuf)));
        device->OnReceive(msg);
    }
    void clear() { test_socket->storage.clear(); }

  protected:
    GuestStatus mGuestStatus;
    MockNotificationSource mNotificationSource;
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;
    TestConnectorRegistry registry;
    IGuestStatusDevice* device;
    TestHalSocket* test_socket;
};

TEST_F(GuestStatusDeviceTest, canCreateDevice) {
    EXPECT_NE(device, nullptr);
}

TEST_F(GuestStatusDeviceTest, heartbeatIncrements) {
    for (unsigned i = 1; i <= 10; i++) {
        receive("heartbeat\0"sv);
        EXPECT_THAT(mGuestStatus.heartbeat.GetValue(), i);
    }
}

TEST_F(GuestStatusDeviceTest, registersResetHandler) {
    EXPECT_NE(sResetHandler, nullptr);
    EXPECT_EQ(sOpaque, device);
}

TEST_F(GuestStatusDeviceTest, receivesBootCompletedEvent) {
    TestSystem test("/");

    test.SetProcessTimes({
        .user_ms = 1,
        .system_ms = 10,
        .wall_clock_ms = 100,
    });

    receive("bootcomplete\0"sv);

    EXPECT_THAT(ToInt64Milliseconds(mGuestStatus.bootcomplete.GetValue() - absl::UnixEpoch()),
                Eq(100));
}

TEST_F(GuestStatusDeviceTest, resetHandlerResetsBootCompleted) {
    TestSystem test("/");

    test.SetProcessTimes({
        .user_ms = 1,
        .system_ms = 10,
        .wall_clock_ms = 100,
    });

    receive("bootcomplete\0"sv);

    EXPECT_THAT(ToInt64Milliseconds(mGuestStatus.bootcomplete.GetValue() - absl::UnixEpoch()),
                Eq(100));

    test.SetProcessTimes({
        .user_ms = 2,
        .system_ms = 20,
        .wall_clock_ms = 200,
    });

    sResetHandler(sOpaque);

    EXPECT_THAT(mGuestStatus.bootcomplete.GetValue(), Eq(absl::UnixEpoch()));

    EXPECT_THAT(ToInt64Milliseconds(mGuestStatus.reset.GetValue() - absl::UnixEpoch()), Eq(200));
}

TEST_F(GuestStatusDeviceTest, sendsNotificationOnBootComplete) {
    TestSystem test("/");

    test.SetProcessTimes({
        .user_ms = 1,
        .system_ms = 10,
        .wall_clock_ms = 1000,
    });

    // Reset marks the start of boot
    sResetHandler(sOpaque);

    test.SetProcessTimes({
        .user_ms = 2,
        .system_ms = 20,
        .wall_clock_ms = 5000,
    });

    EXPECT_CALL(mNotificationSource, FireEvent(::testing::Property(
                                             &avd_universe::grpc::GrpcNotification::booted,
                                             ::testing::Property(&android::emulation::control::BootCompletedNotification::time, Eq(4000)))))
            .Times(1);

    receive("bootcomplete\0"sv);
}

}  // namespace goldfish::devices::guest_status
