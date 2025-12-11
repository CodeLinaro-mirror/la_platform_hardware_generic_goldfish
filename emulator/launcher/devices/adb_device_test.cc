#include "adb_device.h"

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline_definitions.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Return;
using ::testing::ReturnRef;

TEST(AdbDeviceTest, DefaultPort) {
    FakeEmulator emu;

    AdbDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(AdbDeviceTest, CustomPortFromPort) {
    EmulatorPorts ports{.adb_port = 5581};
    FakeEmulator emu(std::move(ports), {});

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"), Eq("virtio-goldfish-adb,host_port=5581")));
}

TEST(AdbDeviceTest, CustomPortFromPorts) {
    EmulatorPorts ports{.serial_number = 7777, .adb_port = 5581};
    FakeEmulator emu(std::move(ports), {});

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"), Eq("virtio-goldfish-adb,host_port=5581")));
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
