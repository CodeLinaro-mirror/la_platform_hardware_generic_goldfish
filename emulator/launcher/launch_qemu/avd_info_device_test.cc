#include "avd_info_device.h"

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Return;
using ::testing::ReturnRef;

TEST(AvdInfoDeviceTest, Basic) {
    FakeEmulator emu;

    AvdInfoDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(Eq("-device"), ::testing::HasSubstr("avdstart,")));
}

TEST(AvdInfoDeviceTest, DefaultEnablesSnapshotUpdateTime) {
    FakeEmulator emu;

    AvdInfoDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(
                        Eq("-device"),
                        ::testing::AllOf(::testing::HasSubstr("avdstart,"),
                                         ::testing::HasSubstr("snapshot_update_time=true"))));
}

TEST(AvdInfoDeviceTest, NoSnapshotUpdateTimeDisablesSnapshotUpdateTime) {
    AndroidOptions opts{};
    opts.no_snapshot_update_time = true;
    FakeEmulator emu(opts);

    AvdInfoDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(
                        Eq("-device"),
                        ::testing::AllOf(::testing::HasSubstr("avdstart,"),
                                         ::testing::HasSubstr("snapshot_update_time=false"))));
}

TEST(AvdInfoDeviceTest, SetsBuildFingerprint) {
    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), BuildFingerprint())
            .WillRepeatedly(
                    Return("google/sdk_gphone64_arm64/emu64a:15/AP3A/12345:userdebug/dev-keys"));

    AvdInfoDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(
                        Eq("-device"),
                        ::testing::HasSubstr("build_fingerprint=google/sdk_gphone64_arm64/"
                                             "emu64a:15/AP3A/12345:userdebug/dev-keys")));
}

}  // namespace android::goldfish::test
