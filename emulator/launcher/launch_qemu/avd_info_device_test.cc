#include "avd_info_device.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "emulator_config.h"
#include "fake_emulator.h"
#include "goldfish/metrics/configure_metrics_writer.h"
#include "snapshot_device.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StartsWith;

TEST(AvdInfoDeviceTest, Basic) {
    FakeEmulator emu;

    AvdInfoDevice dev(/*factory_boot=*/false);
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(Eq("-device"), ::testing::HasSubstr("avdstart,")));
}

TEST(AvdInfoDeviceTest, DefaultEnablesSnapshotUpdateTime) {
    FakeEmulator emu;

    AvdInfoDevice dev(/*factory_boot=*/false);
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

    AvdInfoDevice dev(/*factory_boot=*/false);
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(
                        Eq("-device"),
                        ::testing::AllOf(::testing::HasSubstr("avdstart,"),
                                         ::testing::HasSubstr("snapshot_update_time=false"))));
}

TEST(AvdInfoDeviceTest, SnapshotsEnabledByDefaultAndVersionPassedToPlugin) {
    FakeEmulator emu;

    AvdInfoDevice dev(/*factory_boot=*/false);
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(
                        Eq("-device"),
                        ::testing::AllOf(::testing::HasSubstr("avdstart,"),
                                         ::testing::HasSubstr("snapshots_enabled=true"),
                                         ::testing::HasSubstr(absl::StrCat(
                                                 "snapshot_version=",
                                                 SnapshotVersionFor(kDefaultVirtioGpuDevice))))));
}

TEST(AvdInfoDeviceTest, SnapshotVersionEncodesSelectedVirtioGpuDevice) {
    for (const char* const name : {"gfxstream", "rutabaga"}) {
        AndroidOptions opts{};
        opts.virtio_gpu_device = const_cast<char*>(name);
        FakeEmulator emu(opts);
        const VirtioGpuDevice device = std::string_view(name) == "gfxstream"
                                               ? VirtioGpuDevice::kGfxstream
                                               : VirtioGpuDevice::kRutabaga;

        AvdInfoDevice dev(/*factory_boot=*/false);
        EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
        EXPECT_THAT(
                dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(
                        Eq("-device"),
                        ::testing::AllOf(
                                ::testing::HasSubstr("avdstart,"),
                                ::testing::HasSubstr(absl::StrCat(
                                        "snapshot_version=", SnapshotVersionFor(device), ",")))))
                << name;
    }
    // The two values differ only in the device bits.
    EXPECT_NE(SnapshotVersionFor(VirtioGpuDevice::kGfxstream),
              SnapshotVersionFor(VirtioGpuDevice::kRutabaga));
    EXPECT_EQ(::goldfish::snapshot::SnapshotVersionGpuDevice(
                      SnapshotVersionFor(VirtioGpuDevice::kGfxstream)),
              ::goldfish::snapshot::SnapshotGpuDevice::kGfxstream);
    EXPECT_EQ(::goldfish::snapshot::SnapshotVersionGpuDevice(
                      SnapshotVersionFor(VirtioGpuDevice::kRutabaga)),
              ::goldfish::snapshot::SnapshotGpuDevice::kRutabaga);
}

TEST(AvdInfoDeviceTest, NoSnapshotDisablesSnapshots) {
    AndroidOptions opts{};
    opts.no_snapshot = true;
    FakeEmulator emu(opts);

    AvdInfoDevice dev(/*factory_boot=*/false);
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(
                        Eq("-device"),
                        ::testing::AllOf(::testing::HasSubstr("avdstart,"),
                                         ::testing::HasSubstr("snapshots_enabled=false"))));
}

TEST(AvdInfoDeviceTest, NoSnapshotLoadOrSaveKeepsSnapshotsEnabled) {
    for (const bool load_only : {true, false}) {
        AndroidOptions opts{};
        if (load_only) {
            opts.no_snapshot_load = true;
        } else {
            opts.no_snapshot_save = true;
        }
        FakeEmulator emu(opts);

        AvdInfoDevice dev(/*factory_boot=*/false);
        EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
        EXPECT_THAT(dev.getQemuParameters(emu.config()),
                    ::testing::ElementsAre(
                            Eq("-device"),
                            ::testing::AllOf(::testing::HasSubstr("avdstart,"),
                                             ::testing::HasSubstr("snapshots_enabled=true"))));
    }
}

TEST(AvdInfoDeviceTest, SetsBuildFingerprint) {
    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), BuildFingerprint())
            .WillRepeatedly(
                    Return("google/sdk_gphone64_arm64/emu64a:15/AP3A/12345:userdebug/dev-keys"));

    AvdInfoDevice dev(/*factory_boot=*/false);
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

    AvdInfoDevice dev(/*factory_boot=*/false);
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

    AvdInfoDevice dev(/*factory_boot=*/false);
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
                                emu.emulator_paths(), emu.mock_avd(), opts, emu.feature_flags());

    AvdInfoDevice dev(/*factory_boot=*/false);
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

namespace {

// Returns the avdstart properties that AvdInfoDevice generates for `opts` when the host timezone is
// `host_timezone`.
std::string GetAvdStartProperties(const AndroidOptions& opts,
                                  std::optional<std::string> host_timezone, bool factory_boot) {
    FakeEmulator emu;
    const EmulatorPorts ports{};
    const ChardevEndpoints chardev_endpoints{};
    const UserPaths user_paths{};
    const EmulatorConfig config(ports, chardev_endpoints, MetricsConfig{}, user_paths,
                                emu.emulator_paths(), emu.mock_avd(), opts, emu.feature_flags(),
                                std::move(host_timezone));

    AvdInfoDevice dev(factory_boot);
    EXPECT_THAT(dev.initialize(config), absl_testing::IsOk());
    const std::vector<std::string> params = dev.getQemuParameters(config);
    EXPECT_THAT(params, ElementsAre(Eq("-device"), StartsWith("avdstart,")));
    return params.back();
}

}  // namespace

TEST(AvdInfoDeviceTest, PassesHostTimezoneOnFactoryBoot) {
    EXPECT_THAT(GetAvdStartProperties(AndroidOptions{}, "Asia/Tokyo", /*factory_boot=*/true),
                HasSubstr(",timezone=Asia/Tokyo,"));
}

// The guest copies the timezone into persist.sys.timezone, which would replace the timezone
// selected in the guest (b/532790747).
TEST(AvdInfoDeviceTest, DoesNotPassHostTimezoneAfterFactoryBoot) {
    EXPECT_THAT(GetAvdStartProperties(AndroidOptions{}, "Asia/Tokyo", /*factory_boot=*/false),
                Not(HasSubstr(",timezone=")));
}

TEST(AvdInfoDeviceTest, PassesRequestedTimezoneOnEveryBoot) {
    std::string timezone = "America/New_York";
    const AndroidOptions opts{.timezone = timezone.data()};
    EXPECT_THAT(GetAvdStartProperties(opts, "Asia/Tokyo", /*factory_boot=*/false),
                HasSubstr(",timezone=America/New_York,"));
    EXPECT_THAT(GetAvdStartProperties(opts, "Asia/Tokyo", /*factory_boot=*/true),
                HasSubstr(",timezone=America/New_York,"));
}

TEST(AvdInfoDeviceTest, IgnoresRequestedTimezoneThatIsNotAZoneinfoName) {
    std::string timezone = "JST";
    const AndroidOptions opts{.timezone = timezone.data()};
    EXPECT_THAT(GetAvdStartProperties(opts, "Asia/Tokyo", /*factory_boot=*/false),
                Not(HasSubstr(",timezone=")));
    EXPECT_THAT(GetAvdStartProperties(opts, "Asia/Tokyo", /*factory_boot=*/true),
                HasSubstr(",timezone=Asia/Tokyo,"));
}

TEST(AvdInfoDeviceTest, OmitsUnknownHostTimezone) {
    EXPECT_THAT(GetAvdStartProperties(AndroidOptions{}, std::nullopt, /*factory_boot=*/true),
                Not(HasSubstr(",timezone=")));
}

}  // namespace android::goldfish::test
