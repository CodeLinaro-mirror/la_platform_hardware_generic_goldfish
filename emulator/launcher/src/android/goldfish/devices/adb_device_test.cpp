#include "android/goldfish/devices/adb_device.h"

#include <gtest/gtest.h>

#include <memory>

#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline-definitions.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"
#include "mock_avd.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Return;
using ::testing::ReturnRef;

TEST(AdbDeviceTest, DefaultPort) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                ElementsAre(Eq("-device"),
                            Eq("virtio-goldfish-adb,host_port=5555")));
}

TEST(AdbDeviceTest, CustomPortFromPort) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{.port = "6666"};
    Emulator emu(std::move(avd), std::move(opts));

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                ElementsAre(Eq("-device"),
                            Eq("virtio-goldfish-adb,host_port=6667")));
}

TEST(AdbDeviceTest, CustomPortFromPorts) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{.ports = "7777,8888"};
    Emulator emu(std::move(avd), std::move(opts));

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                ElementsAre(Eq("-device"),
                            Eq("virtio-goldfish-adb,host_port=8888")));
}

TEST(AdbDeviceTest, InvalidPorts) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{.ports = "1234"};
    Emulator emu(std::move(avd), std::move(opts));

    AdbDevice dev;
    EXPECT_FALSE(dev.initialize(emu).ok());
}

TEST(AdbDeviceTest, NonNumericPort) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{.port = "abc"};
    Emulator emu(std::move(avd), std::move(opts));

    AdbDevice dev;
    EXPECT_FALSE(dev.initialize(emu).ok());
}

TEST(AdbDeviceTest, NonNumericPorts) {
    auto hw = HardwareConfig();
    auto avd = std::make_unique<MockAvd>();
    MockAvd* avd_ptr = avd.get();

    AndroidOptions opts{.ports = "123,abc"};
    Emulator emu(std::move(avd), std::move(opts));

    AdbDevice dev;
    EXPECT_FALSE(dev.initialize(emu).ok());
}

}  // namespace android::goldfish::test
