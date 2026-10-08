#include "adb_device.h"

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "android/base/testing/test_system.h"
#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::UnorderedElementsAre;

TEST(AdbDeviceTest, DefaultPort) {
    FakeEmulator emu;

    AdbDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(AdbDeviceTest, CustomPortFromPort) {
    base::TestSystem sys("");
    EmulatorPorts ports{.adb_port = 5581};
    FakeEmulator emu(std::move(ports), {});

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"), Eq("virtio-goldfish-adb,host_port=5581")));
    EXPECT_THAT(dev.getQemuEnv(emu.config()), IsEmpty());
}

TEST(AdbDeviceTest, PassesThroughAdbServerPortEnv) {
    base::TestSystem sys("");
    sys.EnvSet("ANDROID_ADB_SERVER_PORT", "5038");
    EmulatorPorts ports{.adb_port = 5581};
    FakeEmulator emu(std::move(ports), {});

    AdbDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("ANDROID_ADB_SERVER_PORT", "5038")));
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
