#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline-definitions.h"

#include "adb_device.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Return;
using ::testing::ReturnRef;

TEST(AdbDeviceTest, DefaultPort) {
    FakeEmulator emu;

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"),
                            Eq("virtio-goldfish-adb,host_port=5555")));
}

TEST(AdbDeviceTest, CustomPortFromPort) {
    AndroidOptions opts{.port = "6666"};
    FakeEmulator emu(std::move(opts));

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"),
                            Eq("virtio-goldfish-adb,host_port=6667")));
}

TEST(AdbDeviceTest, CustomPortFromPorts) {
    AndroidOptions opts{.ports = "7777,8888"};
    FakeEmulator emu(std::move(opts));

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"),
                            Eq("virtio-goldfish-adb,host_port=8888")));
}

TEST(AdbDeviceTest, InvalidPorts) {
    AndroidOptions opts{.ports = "1234"};
    FakeEmulator emu(std::move(opts));

    AdbDevice dev;
    EXPECT_FALSE(dev.initialize(emu.config()).ok());
}

TEST(AdbDeviceTest, NonNumericPort) {
    AndroidOptions opts{.port = "abc"};
    FakeEmulator emu(std::move(opts));

    AdbDevice dev;
    EXPECT_FALSE(dev.initialize(emu.config()).ok());
}

TEST(AdbDeviceTest, NonNumericPorts) {
    AndroidOptions opts{.ports = "123,abc"};
    FakeEmulator emu(std::move(opts));

    AdbDevice dev;
    EXPECT_FALSE(dev.initialize(emu.config()).ok());
}

}  // namespace android::goldfish::test
