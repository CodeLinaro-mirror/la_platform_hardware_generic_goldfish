// Copyright (C) 2026 The Android Open Source Project
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

#include "user_data_drive.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"

#include "android/base/testing/test_temp_dir.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/file/file.h"
#include "mock_avd.h"

namespace android::goldfish {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::android::base::TestTempDir;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::ReturnRef;

class UserDataDriveTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(tmp_dir_.MakeSubDir("user"));
        ASSERT_TRUE(tmp_dir_.MakeSubDir("init_data"));
        user_dir_ = tmp_dir_.MakeSubPath("user");
        init_data_dir_ = tmp_dir_.MakeSubPath("init_data");

        EXPECT_CALL(mock_avd_, Hw()).WillRepeatedly(ReturnRef(hw_));
        EXPECT_CALL(mock_avd_, GetContentPath()).WillRepeatedly(Return(user_dir_));
    }

    TestTempDir tmp_dir_{"user_data_drive_test"};
    fs::path user_dir_;
    fs::path init_data_dir_;
    HardwareConfig hw_;
    MockAvd mock_avd_;
};

TEST_F(UserDataDriveTest, InitDataNotADirectoryReturnsInvalidArgument) {
    const fs::path non_existent = tmp_dir_.MakeSubPath("does_not_exist");
    EXPECT_THAT(prepareUserdataSrcDir(mock_avd_, non_existent),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(non_existent.string())));

    ASSERT_TRUE(tmp_dir_.MakeSubFile("some_file"));
    const fs::path regular_file = tmp_dir_.MakeSubPath("some_file");
    EXPECT_THAT(prepareUserdataSrcDir(mock_avd_, regular_file),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(regular_file.string())));
}

TEST_F(UserDataDriveTest, EmptyDataDiskMarkerReturnsNullopt) {
    ASSERT_TRUE(tmp_dir_.MakeSubFile("init_data/empty_data_disk"));
    EXPECT_THAT(prepareUserdataSrcDir(mock_avd_, init_data_dir_), IsOkAndHolds(std::nullopt));
}

TEST_F(UserDataDriveTest, NoHingeAndNoMarkerReturnsNullopt) {
    EXPECT_THAT(prepareUserdataSrcDir(mock_avd_, init_data_dir_), IsOkAndHolds(std::nullopt));
}

TEST_F(UserDataDriveTest, HingeSensorMissingFoldDirReturnsInvalidArgument) {
    hw_.hw_sensor_hinge = true;
    const fs::path fold_src_dir = init_data_dir_ / "misc" / "pixel_9_pro_fold";

    EXPECT_THAT(prepareUserdataSrcDir(mock_avd_, init_data_dir_),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(fold_src_dir.string())));
    EXPECT_FALSE(base::file::exists(user_dir_ / "data"));

    ASSERT_TRUE(tmp_dir_.MakeSubDir("user/data"));
    ASSERT_TRUE(tmp_dir_.MakeSubFile("user/data/existing.txt"));
    ASSERT_TRUE(tmp_dir_.MakeSubDir("init_data/misc"));
    ASSERT_TRUE(tmp_dir_.MakeSubFile("init_data/misc/pixel_9_pro_fold"));
    EXPECT_THAT(prepareUserdataSrcDir(mock_avd_, init_data_dir_),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(fold_src_dir.string())));
    EXPECT_TRUE(base::file::exists(user_dir_ / "data" / "existing.txt"));
    EXPECT_FALSE(base::file::exists(user_dir_ / "data" / "system"));
}

TEST_F(UserDataDriveTest, HingeSensorCopiesFoldDataAndWipesStaleScratchDir) {
    hw_.hw_sensor_hinge = true;

    ASSERT_TRUE(tmp_dir_.MakeSubDir("user/data"));
    ASSERT_TRUE(tmp_dir_.MakeSubFile("user/data/stale.txt"));
    const fs::path scratch_dir = tmp_dir_.MakeSubPath("user/data");
    const fs::path stale_file = tmp_dir_.MakeSubPath("user/data/stale.txt");

    ASSERT_TRUE(tmp_dir_.MakeSubDir("init_data/misc/pixel_9_pro_fold"));
    const fs::path fold_src_dir = tmp_dir_.MakeSubPath("init_data/misc/pixel_9_pro_fold");
    {
        std::ofstream ofs(fold_src_dir / "display_settings.xml");
        ofs << "<display-settings/>";
    }

    EXPECT_THAT(prepareUserdataSrcDir(mock_avd_, init_data_dir_),
                IsOkAndHolds(std::optional<fs::path>(scratch_dir)));

    EXPECT_FALSE(base::file::exists(stale_file));
    const fs::path copied_file = scratch_dir / "system" / "display_settings.xml";
    EXPECT_TRUE(base::file::exists(copied_file));

    std::string content;
    std::ifstream ifs(copied_file);
    ifs >> content;
    EXPECT_EQ(content, "<display-settings/>");
}

}  // namespace
}  // namespace android::goldfish
