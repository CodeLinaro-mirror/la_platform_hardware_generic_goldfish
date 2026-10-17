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
#include "goldfish/snapshot/snapshot_store.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"

#include "android/base/testing/test_temp_dir.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/file/file.h"

namespace goldfish::snapshot {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::android::base::TestTempDir;
using ::testing::Eq;

// An arbitrary version for the store under test; the real value is composed
// by the launcher from kSnapshotVersionBase and the gfxstream version.
constexpr int kTestGfxstreamVersion = 7;
constexpr int kTestVersion = MakeSnapshotVersion(kTestGfxstreamVersion);
static_assert(kTestVersion == (7 << 16 | kSnapshotVersionBase));

class SnapshotStoreTest : public ::testing::Test {
  protected:
    void WriteFile(const std::filesystem::path& path, std::string_view contents) {
        ASSERT_OK(android::base::file::mkdir_recursive(path.parent_path(), 0755));
        std::ofstream ofs(path);
        ofs << contents;
    }

    TestTempDir temp_dir_{"snapshot_store_test"};
    FileSnapshotStore store_{temp_dir_.Path(), kTestVersion};
};

TEST(MakeSnapshotVersionTest, LayoutAndHistory) {
    EXPECT_EQ(MakeSnapshotVersion(1) & 0xFFFF, kSnapshotVersionBase);
    EXPECT_EQ(MakeSnapshotVersion(1) >> 16, 1);
    EXPECT_GT(MakeSnapshotVersion(1), 0);
    EXPECT_GT(MakeSnapshotVersion((1 << 15) - 1), 0) << "bit 31 must stay clear";
    EXPECT_EQ(kSnapshotVersionBase, 2);
}

TEST(IsValidSnapshotNameTest, AcceptsPlainNames) {
    EXPECT_TRUE(IsValidSnapshotName("default_boot"));
    EXPECT_TRUE(IsValidSnapshotName("snap_20260101_120000_1"));
    EXPECT_TRUE(IsValidSnapshotName("a"));
    EXPECT_TRUE(IsValidSnapshotName("with.dot"));
    EXPECT_TRUE(IsValidSnapshotName("with space"));
    EXPECT_TRUE(IsValidSnapshotName("快照_2026"));
}

TEST(IsValidSnapshotNameTest, RejectsEmptyAndPathLikeNames) {
    EXPECT_FALSE(IsValidSnapshotName(""));
    EXPECT_FALSE(IsValidSnapshotName("."));
    EXPECT_FALSE(IsValidSnapshotName("a/b"));
    EXPECT_FALSE(IsValidSnapshotName("a\\b"));
    EXPECT_FALSE(IsValidSnapshotName(".."));
    EXPECT_FALSE(IsValidSnapshotName("../x"));
    EXPECT_FALSE(IsValidSnapshotName("x.."));
    EXPECT_FALSE(IsValidSnapshotName("/abs"));
    for (const char* name : {"a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b"}) {
        SCOPED_TRACE(name);
        EXPECT_FALSE(IsValidSnapshotName(name));
    }
}

TEST(ValidateSnapshotNameTest, ExplainsTheRules) {
    EXPECT_OK(ValidateSnapshotName("default_boot"));
    for (const char* name :
         {"", ".", "..", "a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b"}) {
        SCOPED_TRACE(name);
        const absl::Status s = ValidateSnapshotName(name);
        EXPECT_THAT(s, StatusIs(absl::StatusCode::kInvalidArgument));
        EXPECT_THAT(s.message(), ::testing::AllOf(::testing::HasSubstr("Invalid snapshot name"),
                                                  ::testing::HasSubstr("not '.'"),
                                                  ::testing::HasSubstr("'..'")));
    }
}

TEST_F(SnapshotStoreTest, PathsAreNestedUnderSnapshotsRoot) {
    EXPECT_EQ(store_.Root(), temp_dir_.Path() / "snapshots");
    EXPECT_EQ(store_.Dir("foo"), temp_dir_.Path() / "snapshots" / "foo");
}

TEST_F(SnapshotStoreTest, ReadVersionNotFoundWithoutVersionFile) {
    EXPECT_FALSE(store_.Exists("foo"));
    EXPECT_THAT(store_.ReadVersion("foo"), StatusIs(absl::StatusCode::kNotFound));
    EXPECT_FALSE(store_.IsCompatible("foo"));

    // A directory without version.txt is "present" but not compatible.
    ASSERT_OK(store_.Prepare("foo"));
    EXPECT_TRUE(store_.Exists("foo"));
    EXPECT_THAT(store_.ReadVersion("foo"), StatusIs(absl::StatusCode::kNotFound));
    EXPECT_FALSE(store_.IsCompatible("foo"));
}

TEST_F(SnapshotStoreTest, WriteVersionCreatesDirectoryAndRecordsCurrentVersion) {
    ASSERT_OK(store_.WriteVersion("foo"));
    EXPECT_TRUE(android::base::file::is_dir(store_.Dir("foo")));
    EXPECT_TRUE(android::base::file::is_file(store_.Dir("foo") / "version.txt"));
    EXPECT_THAT(store_.ReadVersion("foo"), IsOkAndHolds(kTestVersion));
    EXPECT_TRUE(store_.IsCompatible("foo"));
}

TEST_F(SnapshotStoreTest, WriteAndReadVersionWithUnicodeSnapshotName) {
    constexpr std::string_view kUnicodeName = "快照_2026";
    EXPECT_THAT(store_.ReadVersion(kUnicodeName), StatusIs(absl::StatusCode::kNotFound));
    ASSERT_OK(store_.WriteVersion(kUnicodeName));
    EXPECT_TRUE(store_.Exists(kUnicodeName));
    EXPECT_THAT(store_.ReadVersion(kUnicodeName), IsOkAndHolds(kTestVersion));
    EXPECT_TRUE(store_.IsCompatible(kUnicodeName));
    ASSERT_OK(store_.Remove(kUnicodeName));
    EXPECT_FALSE(store_.Exists(kUnicodeName));
}

TEST_F(SnapshotStoreTest, WriteVersionOverwritesStaleVersion) {
    WriteFile(store_.Dir("foo") / "version.txt", "1");
    EXPECT_THAT(store_.ReadVersion("foo"), IsOkAndHolds(1));
    EXPECT_FALSE(store_.IsCompatible("foo"));

    ASSERT_OK(store_.WriteVersion("foo"));
    EXPECT_THAT(store_.ReadVersion("foo"), IsOkAndHolds(kTestVersion));
    EXPECT_TRUE(store_.IsCompatible("foo"));
}

TEST_F(SnapshotStoreTest, ReadVersionToleratesTrailingWhitespace) {
    WriteFile(store_.Dir("foo") / "version.txt", "42\n");
    EXPECT_THAT(store_.ReadVersion("foo"), IsOkAndHolds(42));
}

TEST_F(SnapshotStoreTest, ReadVersionReportsUnparsableFile) {
    WriteFile(store_.Dir("foo") / "version.txt", "not-a-number");
    EXPECT_THAT(store_.ReadVersion("foo"), StatusIs(absl::StatusCode::kDataLoss));
    EXPECT_FALSE(store_.IsCompatible("foo"));
}

TEST_F(SnapshotStoreTest, IncompatibleWhenBaseOrGfxstreamVersionDiffers) {
    WriteFile(store_.Dir("base") / "version.txt",
              std::to_string((kTestGfxstreamVersion << 16) | (kSnapshotVersionBase + 1)));
    EXPECT_FALSE(store_.IsCompatible("base"));

    WriteFile(store_.Dir("gfx") / "version.txt",
              std::to_string(MakeSnapshotVersion(kTestGfxstreamVersion - 1)));
    EXPECT_FALSE(store_.IsCompatible("gfx"));
    EXPECT_EQ(store_.Version(), kTestVersion);
}

TEST_F(SnapshotStoreTest, RemoveDeletesWholeSnapshotTreeAndOnlyThat) {
    ASSERT_OK(store_.WriteVersion("foo"));
    ASSERT_OK(store_.WriteVersion("bar"));
    WriteFile(store_.Dir("foo") / "renderer" / "rutabaga-1" / "blob", "x");

    ASSERT_OK(store_.Remove("foo"));
    EXPECT_FALSE(android::base::file::exists(store_.Dir("foo")));
    EXPECT_FALSE(store_.Exists("foo"));
    EXPECT_TRUE(store_.Exists("bar"));
    EXPECT_TRUE(store_.IsCompatible("bar"));
    EXPECT_TRUE(android::base::file::is_dir(store_.Root()));
}

TEST_F(SnapshotStoreTest, RemoveIsIdempotent) {
    EXPECT_OK(store_.Remove("missing"));
    ASSERT_OK(store_.WriteVersion("foo"));
    EXPECT_OK(store_.Remove("foo"));
    EXPECT_OK(store_.Remove("foo"));
}

TEST_F(SnapshotStoreTest, RemoveOfDotDoesNotDeleteTheSnapshotsRoot) {
    ASSERT_OK(store_.WriteVersion("keep"));
    EXPECT_THAT(store_.Remove("."), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(store_.Remove(".."), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_TRUE(store_.IsCompatible("keep"));
    EXPECT_TRUE(android::base::file::is_dir(store_.Root()));
}

TEST_F(SnapshotStoreTest, InvalidNamesAreRejectedBeforeTouchingTheFileSystem) {
    for (const char* name : {"", ".", "..", "../escape", "a/b", "a\\b", "a:b", "a*b", "a?b",
                             "a\"b", "a<b", "a>b", "a|b"}) {
        SCOPED_TRACE(name);
        EXPECT_FALSE(store_.Exists(name));
        EXPECT_THAT(store_.ReadVersion(name), StatusIs(absl::StatusCode::kInvalidArgument));
        EXPECT_FALSE(store_.IsCompatible(name));
        EXPECT_THAT(store_.Prepare(name), StatusIs(absl::StatusCode::kInvalidArgument));
        EXPECT_THAT(store_.WriteVersion(name), StatusIs(absl::StatusCode::kInvalidArgument));
        EXPECT_THAT(store_.Remove(name), StatusIs(absl::StatusCode::kInvalidArgument));
    }
    // Nothing escaped the root: the parent of <tmp>/snapshots has only the
    // temp dir itself, and the root was never created.
    EXPECT_FALSE(android::base::file::exists(store_.Root()));
    EXPECT_FALSE(android::base::file::exists(temp_dir_.Path() / "escape"));
}

}  // namespace
}  // namespace goldfish::snapshot
