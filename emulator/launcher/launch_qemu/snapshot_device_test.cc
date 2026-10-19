// Copyright 2026 The Android Open Source Project
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

#include "snapshot_device.h"

#include <gtest/gtest.h>

#include <fstream>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "android/base/testing/test_temp_dir.h"
#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"
#include "goldfish/file/file.h"

namespace android::goldfish::test {

using ::android::base::TestTempDir;
using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::ReturnRef;

class SnapshotDeviceTest : public ::testing::Test {
  protected:
    void WriteSnapshotVersion(int version, const char* name = "default_boot") {
        const fs::path snapshot_dir = temp_dir_.Path() / "snapshots" / name;
        ASSERT_OK(base::file::mkdir_recursive(snapshot_dir, 0755));
        std::ofstream ofs(snapshot_dir / "version.txt");
        ofs << version;
    }

    TestTempDir temp_dir_{"snapshot_device_test"};
    HardwareConfig hw_{};
};

TEST_F(SnapshotDeviceTest, ColdBootWhenSnapshotVersionMissing) {
    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedOldSnapshot);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kSaving);
    EXPECT_TRUE(dev.is_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-savevm"), Eq("default_boot")));
}

TEST_F(SnapshotDeviceTest, WarmBootWhenSnapshotVersionMatches) {
    WriteSnapshotVersion(kSnapshotVersion);

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kLoading);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kSaving);
    EXPECT_TRUE(dev.is_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-loadvm"), Eq("default_boot"), Eq("-savevm"), Eq("default_boot")));
}

TEST_F(SnapshotDeviceTest, NoSnapshotDisablesLoadAndSaveViaCmdline) {
    WriteSnapshotVersion(kSnapshotVersion);

    AndroidOptions opts{};
    opts.no_snapshot = true;
    FakeEmulator emu(opts);
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedCmdline);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kDisabledCmdline);
    EXPECT_FALSE(dev.is_enabled());
    EXPECT_FALSE(dev.snapshots_enabled());
    EXPECT_FALSE(SnapshotDevice::are_snapshots_enabled(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), IsEmpty());
}

TEST_F(SnapshotDeviceTest, NoSnapshotLoadSkipsLoadViaCmdline) {
    WriteSnapshotVersion(kSnapshotVersion);

    AndroidOptions opts{};
    opts.no_snapshot_load = true;
    FakeEmulator emu(opts);
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedCmdline);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kSaving);
    EXPECT_TRUE(dev.is_enabled());
    EXPECT_TRUE(dev.snapshots_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-savevm"), Eq("default_boot")));
}

TEST_F(SnapshotDeviceTest, NoSnapstorageSkipsLoadViaCmdline) {
    WriteSnapshotVersion(kSnapshotVersion);

    AndroidOptions opts{};
    opts.no_snapstorage = true;
    FakeEmulator emu(opts);
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedCmdline);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kSaving);
    EXPECT_TRUE(dev.is_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-savevm"), Eq("default_boot")));
}

TEST_F(SnapshotDeviceTest, NoSnapshotSaveDisablesSaveViaCmdline) {
    WriteSnapshotVersion(kSnapshotVersion);

    AndroidOptions opts{};
    opts.no_snapshot_save = true;
    FakeEmulator emu(opts);
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kLoading);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kDisabledCmdline);
    EXPECT_TRUE(dev.is_enabled());
    EXPECT_TRUE(dev.snapshots_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-loadvm"), Eq("default_boot")));
}

TEST_F(SnapshotDeviceTest, ForceColdBootSkipsLoadAndSaveViaAvd) {
    WriteSnapshotVersion(kSnapshotVersion);
    hw_.fastboot_forceColdBoot = true;

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedAvd);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kDisabledAvd);
    EXPECT_FALSE(dev.is_enabled());
    // fastboot.forceColdBoot only governs the Quickboot snapshot.
    EXPECT_TRUE(dev.snapshots_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()), IsEmpty());
}

TEST_F(SnapshotDeviceTest, ColdBootWhenSnapshotVersionMismatches) {
    WriteSnapshotVersion(1);

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedOldSnapshot);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kSaving);
    EXPECT_TRUE(dev.is_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-savevm"), Eq("default_boot")));
}

TEST_F(SnapshotDeviceTest, ColdBootWhenGfxstreamSnapshotVersionMismatches) {
    WriteSnapshotVersion(((kGfxstreamSnapshotVersion - 1) << 16) | kSnapshotVersionBase);

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedOldSnapshot);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kSaving);
    EXPECT_TRUE(dev.is_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-savevm"), Eq("default_boot")));
}

TEST_F(SnapshotDeviceTest, InitializeCreatesDirectoryButDoesNotWriteVersionUpFront) {
    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kSaving);
    const fs::path snapshot_dir = temp_dir_.Path() / "snapshots" / "default_boot";
    EXPECT_TRUE(base::file::is_dir(snapshot_dir));
    // version.txt is only written when a save completes; a crash before the exit-time save must
    // not leave a version file claiming compatibility next to stale or missing qcow2 state.
    EXPECT_FALSE(base::file::exists(snapshot_dir / "version.txt"));
}

TEST_F(SnapshotDeviceTest, InitializeLeavesStaleVersionAloneUntilNextSave) {
    WriteSnapshotVersion(1);

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedOldSnapshot);
    std::ifstream ifs(temp_dir_.Path() / "snapshots" / "default_boot" / "version.txt");
    int version = 0;
    ASSERT_TRUE(ifs >> version);
    EXPECT_EQ(version, 1) << "a cold boot must not rewrite version.txt before the save";
}

TEST_F(SnapshotDeviceTest, InvalidSnapshotNameIsRejectedUpFront) {
    for (const char* name : {"a/b", "my..snap", "."}) {
        SCOPED_TRACE(name);
        AndroidOptions opts{};
        opts.snapshot = const_cast<char*>(name);
        FakeEmulator emu(opts);
        EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

        SnapshotDevice dev;
        EXPECT_THAT(dev.initialize(emu.config()),
                    StatusIs(absl::StatusCode::kInvalidArgument,
                             ::testing::HasSubstr("Invalid snapshot name")));
    }
}

TEST_F(SnapshotDeviceTest, DirectoryErrorWhenSnapshotDirCreationFails) {
    ASSERT_OK(base::file::touch(temp_dir_.Path() / "snapshots"));

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(temp_dir_.Path()));
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw_));

    SnapshotDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.load_status(), SnapshotDevice::LoadStatus::kSkippedOldSnapshot);
    EXPECT_EQ(dev.save_status(), SnapshotDevice::SaveStatus::kDirectoryError);
    EXPECT_FALSE(dev.is_enabled());
    EXPECT_THAT(dev.getQemuParameters(emu.config()), IsEmpty());
}

}  // namespace android::goldfish::test
