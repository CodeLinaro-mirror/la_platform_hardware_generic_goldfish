#include "avd_info_device.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "emulator_config.h"
#include "fake_emulator.h"
#include "goldfish/metrics/configure_metrics_writer.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::HasSubstr;
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

// b/573235492: QEMU splits -device options on single commas, so an unescaped comma in the
// display name turned every fragment of the name into a separate avdstart property.
TEST(AvdInfoDeviceTest, EscapesCommasInDisplayName) {
    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), DisplayName())
            .WillRepeatedly(Return("°º¤ø,¸¸,ø¤º°`°º¤ø, UTF-8 ¸,ø¤°º¤ø,¸¸,ø¤º°`°º¤ø,¸"));
    EXPECT_CALL(emu.mock_avd(), Id()).WillRepeatedly(Return("avd-3808973207"));

    AvdInfoDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"),
                            HasSubstr(",avd_name=°º¤ø,,¸¸,,ø¤º°`°º¤ø,, UTF-8 ¸,,ø¤°º¤ø,,¸¸,,"
                                      "ø¤º°`°º¤ø,,¸,avd_id=avd-3808973207,")));
}

TEST(AvdInfoDeviceTest, EscapesCommasInAvdDir) {
    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath())
            .WillRepeatedly(Return(fs::path("/home/Doe, Jane/.android/avd/test.avd")));

    AvdInfoDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"),
                            HasSubstr(",avd_dir=/home/Doe,, Jane/.android/avd/test.avd,")));
}

namespace {

using ::goldfish::metrics::MetricsWriterType;

constexpr char kMetricsSessionId[] = "a10e1136-6d27-4274-a30c-89e569249f7b";

// Returns the QEMU parameters that AvdInfoDevice generates for `metrics_config`.
std::vector<std::string> GetQemuParametersWithMetrics(MetricsConfig metrics_config) {
    FakeEmulator emu;
    const EmulatorPorts ports{};
    const ChardevEndpoints chardev_endpoints{};
    const UserPaths user_paths{};
    const AndroidOptions opts{};
    const EmulatorConfig config(ports, chardev_endpoints, std::move(metrics_config), user_paths,
                                emu.emulator_paths(), emu.mock_avd(), opts);

    AvdInfoDevice dev;
    EXPECT_THAT(dev.initialize(config), absl_testing::IsOk());
    return dev.getQemuParameters(config);
}

}  // namespace

TEST(AvdInfoDeviceTest, EscapesCommasInMetricsFilePath) {
    EXPECT_THAT(GetQemuParametersWithMetrics(
                        {.session_id = kMetricsSessionId,
                         .writer_config = {.type = MetricsWriterType::kFile,
                                           .file_path = "/home/Doe, Jane/metrics.txt"}}),
                ElementsAre(Eq("-device"),
                            EndsWith(",metrics_file_path=/home/Doe,, Jane/metrics.txt")));
}

TEST(AvdInfoDeviceTest, EscapesCommasInMetricsSpoolDir) {
    EXPECT_THAT(GetQemuParametersWithMetrics(
                        {.session_id = kMetricsSessionId,
                         .writer_config = {.type = MetricsWriterType::kStudio,
                                           .studio_spool_dir = "/home/Doe, Jane/spool"}}),
                ElementsAre(Eq("-device"), EndsWith(",metrics_spool_dir=/home/Doe,, Jane/spool")));
}

TEST(AvdInfoDeviceTest, EscapesCommasInPlaystoreMetricsUrlAndUserId) {
    EXPECT_THAT(GetQemuParametersWithMetrics(
                        {.session_id = kMetricsSessionId,
                         .writer_config = {.type = MetricsWriterType::kPlaystore,
                                           .playstore_url = "https://example.com/log?a=1,2",
                                           .user_id = "user,1"}}),
                ElementsAre(Eq("-device"),
                            EndsWith(",metrics_playstore_url=https://example.com/log?a=1,,2,"
                                     "metrics_user_id=user,,1")));
}

}  // namespace android::goldfish::test
