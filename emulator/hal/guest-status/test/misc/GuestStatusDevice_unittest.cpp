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
#include "android/misc/GuestStatusDevice.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include "absl/strings/numbers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "android/base/testing/TestSystem.h"
#include "goldfish//async/testing/test_event_loop.h"
#include "goldfish/devices/test_connector_registry.h"

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
extern "C" {
void qemu_register_reset(QEMUResetHandler* func, void* opaque) {
    sResetHandler = func;
    sOpaque = opaque;
}
}
}  // namespace

class GuestStatusDeviceTest : public ::testing::Test {
    void SetUp() override {
        mClientLoop = TestEventLoop::create();
        mQemuLoop = TestEventLoop::create();

        IGuestStatusDevice::registerDevice(&registry, {qemu_register_reset, nullptr},
                                           mClientLoop.get(), mQemuLoop.get(), 0);
        device = registry.constructHalDevice<IGuestStatusDevice>();
        test_socket = registry.halSocket();
        clear();
    }

  public:
    void receive(const std::string_view msg) {
        char sizeBuf[sizeof(uint32_t)];
        absl::little_endian::Store32(sizeBuf, msg.size());
        device->onReceive(std::string_view(sizeBuf, sizeof(sizeBuf)));
        device->onReceive(msg);
    }
    void clear() { test_socket->storage.clear(); }

  protected:
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;

    TestConnectorRegistry registry;
    TestHalSocket* test_socket;
    IGuestStatusDevice* device;
};

TEST_F(GuestStatusDeviceTest, canCreateDevice) {
    EXPECT_NE(device, nullptr);
}

TEST_F(GuestStatusDeviceTest, heartbeatSendsAnEvent) {
    auto start = device->heartbeat();
    AndroidGuestStatus received;
    auto scoped = android::base::eventing::makeScopedCallback(
            *device, [&received](AndroidGuestStatus event) { received = event; });
    receive("heartbeat\0"sv);
    EXPECT_THAT(received.heartbeat(), Eq(start + 1));
}

TEST_F(GuestStatusDeviceTest, heartbeatIncrements) {
    auto start = device->heartbeat();
    for (int i = 0; i < 10; i++) {
        receive("heartbeat\0"sv);
    }
    EXPECT_THAT(device->heartbeat(), Eq(10 + start));
}

TEST_F(GuestStatusDeviceTest, receivesBootCompletedEvent) {
    TestSystem test("/");
    AndroidGuestStatus received;
    auto scoped = android::base::eventing::makeScopedCallback(
            *device, [&received](AndroidGuestStatus event) { received = event; });

    test.setProcessTimes({
        .userMs = 1,
        .systemMs = 10,
        .wallClockMs = 100,
    });
    receive("bootcomplete\0"sv);
    EXPECT_THAT(received.isBootCompletedEvent(), Eq(true));
    EXPECT_THAT(received.bootTime().count(), Eq(100));
}

TEST_F(GuestStatusDeviceTest, tracksBootCompleted) {
    TestSystem test("/");
    test.setProcessTimes({
        .userMs = 1,
        .systemMs = 10,
        .wallClockMs = 100,
    });
    receive("bootcomplete\0"sv);
    EXPECT_THAT(device->hasBooted(), Eq(true));
    EXPECT_THAT(device->bootTime()->count(), Eq(100));
}

TEST_F(GuestStatusDeviceTest, registersResetHandler) {
    EXPECT_NE(sResetHandler, nullptr);
    EXPECT_EQ(sOpaque, device);
}

TEST_F(GuestStatusDeviceTest, resetHandlerResetsBootCompleted) {
    using namespace std::chrono_literals;
    // Simulate a reset
    sResetHandler(sOpaque);
    EXPECT_THAT(device->hasBooted(), Eq(false));
    EXPECT_THAT(device->bootTime(), Eq(std::nullopt));
}

TEST_F(GuestStatusDeviceTest, firesResetEvent) {
    AndroidGuestStatus received;
    auto scoped = android::base::eventing::makeScopedCallback(
            *device, [&received](AndroidGuestStatus event) { received = event; });

    receive("bootcomplete\0"sv);

    // Simulate a reset
    sResetHandler(sOpaque);
    EXPECT_THAT(received.isResetEvent(), Eq(true));
}

}  // namespace goldfish::devices::guest_status
