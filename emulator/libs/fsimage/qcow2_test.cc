// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "goldfish/fsimage/qcow2.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/time.h"

#include "android/base/testing/test_temp_dir.h"
#include "qcow2_internal.h"

namespace goldfish::fsimage {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::android::base::TestTempDir;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

uint16_t ReadBe16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8U) | static_cast<uint16_t>(p[1]));
}

uint32_t ReadBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24U) | (static_cast<uint32_t>(p[1]) << 16U) |
           (static_cast<uint32_t>(p[2]) << 8U) | static_cast<uint32_t>(p[3]);
}

uint64_t ReadBe64(const uint8_t* p) {
    return (static_cast<uint64_t>(ReadBe32(p)) << 32U) | static_cast<uint64_t>(ReadBe32(p + 4));
}

void WriteBe16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
    p[1] = static_cast<uint8_t>(v & 0xFFU);
}

void WriteBe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24U) & 0xFFU);
    p[1] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
    p[2] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
    p[3] = static_cast<uint8_t>(v & 0xFFU);
}

void WriteBe64(uint8_t* p, uint64_t v) {
    WriteBe32(p, static_cast<uint32_t>(v >> 32U));
    WriteBe32(p + 4, static_cast<uint32_t>(v & 0xFFFFFFFFULL));
}

void WriteFile(const fs::path& path, std::string_view content) {
    std::ofstream ofs(path, std::ios_base::binary);
    ASSERT_TRUE(ofs.good());
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    ASSERT_TRUE(ofs.good());
}

std::vector<uint8_t> ReadFileBytes(const fs::path& path) {
    std::ifstream ifs(path, std::ios_base::binary | std::ios_base::ate);
    EXPECT_TRUE(ifs.good());
    const auto size = ifs.tellg();
    ifs.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(buf.data()), size);
    EXPECT_TRUE(ifs.good());
    return buf;
}

TEST(IsQcow2PathTest, MissingFile) {
    TestTempDir temp_dir("qcow2_test");
    EXPECT_FALSE(IsQcow2Path(temp_dir.Path() / "nonexistent.qcow2"));
}

TEST(IsQcow2PathTest, EmptyFile) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path path = temp_dir.Path() / "empty.qcow2";
    WriteFile(path, "");
    EXPECT_FALSE(IsQcow2Path(path));
}

TEST(IsQcow2PathTest, TooShort) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path path = temp_dir.Path() / "short.qcow2";
    WriteFile(path, "QFI");
    EXPECT_FALSE(IsQcow2Path(path));
}

TEST(IsQcow2PathTest, WrongMagic) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path path = temp_dir.Path() / "wrong_magic.img";
    WriteFile(path, "QFI\xFA\x00\x00\x00\x03");
    EXPECT_FALSE(IsQcow2Path(path));
}

TEST(IsQcow2PathTest, TruncatedQcow2Header) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path path = temp_dir.Path() / "magic_only.qcow2";
    WriteFile(path, "QFI\xFB");
    EXPECT_FALSE(IsQcow2Path(path));
}

TEST(IsQcow2PathTest, ValidQcow2Overlay) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path path = temp_dir.Path() / "valid.qcow2";
    ASSERT_THAT(internal::CreateQcow2Overlay(path, "backing.img", "raw", 1024 * 1024), IsOk());
    EXPECT_TRUE(IsQcow2Path(path));
}

TEST(ReadQcow2VirtualSizeTest, MissingFileReturnsError) {
    TestTempDir temp_dir("qcow2_test");
    EXPECT_THAT(internal::ReadQcow2VirtualSize(temp_dir.Path() / "nonexistent.qcow2"),
                StatusIs(absl::StatusCode::kInternal));
}

TEST(ReadQcow2VirtualSizeTest, NonQcow2FileReturnsNullopt) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path empty_path = temp_dir.Path() / "empty.img";
    WriteFile(empty_path, "");
    EXPECT_THAT(internal::ReadQcow2VirtualSize(empty_path), IsOkAndHolds(std::nullopt));

    const fs::path raw_path = temp_dir.Path() / "raw.img";
    WriteFile(raw_path, "raw image contents");
    EXPECT_THAT(internal::ReadQcow2VirtualSize(raw_path), IsOkAndHolds(std::nullopt));
}

TEST(ReadQcow2VirtualSizeTest, TruncatedHeaderReturnsDataLoss) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path truncated_path = temp_dir.Path() / "truncated.qcow2";
    WriteFile(truncated_path, "QFI\xFB");
    EXPECT_THAT(internal::ReadQcow2VirtualSize(truncated_path),
                StatusIs(absl::StatusCode::kDataLoss));
}

TEST(ReadQcow2VirtualSizeTest, ValidQcow2ReturnsVirtualSize) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    constexpr uint64_t kVirtualSize = 2 * 1024 * 1024;
    ASSERT_THAT(internal::CreateQcow2Overlay(overlay, "backing.img", "raw", kVirtualSize), IsOk());
    EXPECT_THAT(internal::ReadQcow2VirtualSize(overlay),
                IsOkAndHolds(std::optional<uint64_t>(kVirtualSize)));
}

TEST(CreateQcow2OverlayImplTest, MismatchedBackingPathAndFormatReturnsInvalidArgument) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    EXPECT_THAT(internal::CreateQcow2Overlay(overlay, "", "raw", 1024),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(internal::CreateQcow2Overlay(overlay, "backing.img", "", 1024),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CreateQcow2OverlayImplTest, BackingPathTooLongReturnsInvalidArgument) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    const std::string long_backing(1024, 'a');
    EXPECT_THAT(internal::CreateQcow2Overlay(overlay, long_backing, "raw", 1024),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CreateQcow2OverlayImplTest, RawSizeTooLargeReturnsOutOfRange) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    constexpr uint64_t kMaxVirtualSize =
            (32ULL * 1024 * 1024 / sizeof(uint64_t)) * (512ULL * 1024 * 1024);
    EXPECT_THAT(internal::CreateQcow2Overlay(overlay, "backing.img", "raw", kMaxVirtualSize + 1),
                StatusIs(absl::StatusCode::kOutOfRange));
    EXPECT_THAT(internal::CreateQcow2Overlay(overlay, "backing.img", "raw",
                                             std::numeric_limits<uint64_t>::max()),
                StatusIs(absl::StatusCode::kOutOfRange));
}

TEST(CreateQcow2OverlayImplTest, AlignsUnalignedBackingSizeToSectorBoundary) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "small.img.qcow2";
    ASSERT_THAT(internal::CreateQcow2Overlay(overlay, "small.img", "raw", 11), IsOk());
    EXPECT_THAT(internal::ReadQcow2VirtualSize(overlay),
                IsOkAndHolds(std::optional<uint64_t>(512ULL)));
}

TEST(CreateQcow2OverlayImplTest, WritesExpectedHeaderAndTables) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "userdata.img.qcow2";
    constexpr std::string_view kBackingPath = "/path/to/userdata.img";
    constexpr uint64_t kBackingSize = 1024 * 1024;  // 1 MiB

    ASSERT_THAT(internal::CreateQcow2Overlay(overlay, kBackingPath, "raw", kBackingSize), IsOk());
    EXPECT_TRUE(IsQcow2Path(overlay));
    EXPECT_THAT(internal::ReadQcow2VirtualSize(overlay),
                IsOkAndHolds(std::optional<uint64_t>(kBackingSize)));

    const std::vector<uint8_t> data = ReadFileBytes(overlay);
    constexpr size_t kClusterSize = 65536;
    ASSERT_EQ(data.size(), 4 * kClusterSize);

    // Verify QCOW2 v3 header in Cluster 0.
    EXPECT_EQ(ReadBe32(data.data() + 0), 0x514649FBU);  // magic
    EXPECT_EQ(ReadBe32(data.data() + 4), 3U);           // version = 3
    const uint64_t backing_offset = ReadBe64(data.data() + 8);
    const uint32_t backing_size = ReadBe32(data.data() + 16);
    EXPECT_EQ(backing_offset, 136U);
    EXPECT_EQ(backing_size, kBackingPath.size());
    EXPECT_EQ(ReadBe32(data.data() + 20), 16U);               // cluster_bits = 16 (64 KiB)
    EXPECT_EQ(ReadBe64(data.data() + 24), kBackingSize);      // virtual size
    EXPECT_EQ(ReadBe32(data.data() + 32), 0U);                // crypt_method = none
    EXPECT_EQ(ReadBe32(data.data() + 36), 1U);                // l1_size = 1
    EXPECT_EQ(ReadBe64(data.data() + 40), 3 * kClusterSize);  // l1_table_offset
    EXPECT_EQ(ReadBe64(data.data() + 48), 1 * kClusterSize);  // refcount_table_offset
    EXPECT_EQ(ReadBe32(data.data() + 56), 1U);                // refcount_table_clusters
    EXPECT_EQ(ReadBe32(data.data() + 96), 4U);                // refcount_order = 4 (16-bit)
    EXPECT_EQ(ReadBe32(data.data() + 100), 112U);             // header_length = 112

    // Verify backing format extension ("raw") at offset 112.
    EXPECT_EQ(ReadBe32(data.data() + 112), 0xE2792ACAU);
    EXPECT_EQ(ReadBe32(data.data() + 116), 3U);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(data.data() + 120), 3), "raw");

    // Verify backing file path string at backing_offset.
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(data.data() + backing_offset),
                               backing_size),
              kBackingPath);

    // Verify Cluster 1: Refcount table entry 0 points to Cluster 2.
    EXPECT_EQ(ReadBe64(data.data() + kClusterSize), 2 * kClusterSize);

    // Verify Cluster 2: Refcount block has refcount=1 for clusters 0..3 and 0 afterwards.
    const uint8_t* refblock = data.data() + 2 * kClusterSize;
    EXPECT_EQ(ReadBe16(refblock + 0), 1U);
    EXPECT_EQ(ReadBe16(refblock + 2), 1U);
    EXPECT_EQ(ReadBe16(refblock + 4), 1U);
    EXPECT_EQ(ReadBe16(refblock + 6), 1U);
    EXPECT_EQ(ReadBe16(refblock + 8), 0U);
    EXPECT_FALSE(fs::exists(fs::path(overlay).concat(".tmp")));
}

TEST(CreateQcow2OverlayImplTest, CleansUpTempFileOnRenameError) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay_dir = temp_dir.Path() / "existing_dir.qcow2";
    ASSERT_TRUE(fs::create_directory(overlay_dir));

    EXPECT_THAT(internal::CreateQcow2Overlay(overlay_dir, "backing.img", "raw", 1024),
                StatusIs(absl::StatusCode::kInternal));
    EXPECT_FALSE(fs::exists(fs::path(overlay_dir).concat(".tmp")));
}

TEST(CreateQcow2OverlayTest, CreatesValidOverlayWithoutBackingFile) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "vvfat.qcow2";
    constexpr uint64_t kVirtualSize = 504ULL * 1024 * 1024;  // 504 MiB

    ASSERT_THAT(CreateQcow2Overlay(overlay, kVirtualSize), IsOk());
    EXPECT_TRUE(IsQcow2Path(overlay));
    EXPECT_THAT(internal::ReadQcow2VirtualSize(overlay),
                IsOkAndHolds(std::optional<uint64_t>(kVirtualSize)));

    const std::vector<uint8_t> data = ReadFileBytes(overlay);
    EXPECT_EQ(ReadBe64(data.data() + 8), 0U);    // backing_file_offset = 0
    EXPECT_EQ(ReadBe32(data.data() + 16), 0U);   // backing_file_size = 0
    EXPECT_EQ(ReadBe32(data.data() + 112), 0U);  // QCOW2_EXT_MAGIC_END immediately after header
}

TEST(CreateQcow2OverlayTest, MissingBackingFileReturnsNotFound) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    const fs::path backing = temp_dir.Path() / "missing.img";
    EXPECT_THAT(CreateQcow2Overlay(overlay, backing), StatusIs(absl::StatusCode::kNotFound));
}

TEST(CreateQcow2OverlayTest, CreatesValidOverlayFromRawBackingFile) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path backing = temp_dir.Path() / "userdata.img";
    const fs::path overlay = temp_dir.Path() / "userdata.img.qcow2";

    constexpr size_t kBackingSize = 4096;
    WriteFile(backing, std::string(kBackingSize, '\0'));

    ASSERT_THAT(CreateQcow2Overlay(overlay, backing), IsOk());
    EXPECT_THAT(internal::ReadQcow2VirtualSize(overlay),
                IsOkAndHolds(std::optional<uint64_t>(kBackingSize)));
}

TEST(CreateQcow2OverlayTest, CreatesValidOverlayFromQcow2BackingFile) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay1 = temp_dir.Path() / "overlay1.qcow2";
    const fs::path overlay2 = temp_dir.Path() / "overlay2.qcow2";

    constexpr uint64_t kVirtualSize = 2 * 1024 * 1024;  // 2 MiB
    ASSERT_THAT(internal::CreateQcow2Overlay(overlay1, "base.img", "raw", kVirtualSize), IsOk());
    ASSERT_THAT(CreateQcow2Overlay(overlay2, overlay1), IsOk());

    // Virtual size must match overlay1's 2 MiB virtual size, not its 256 KiB file size.
    EXPECT_THAT(internal::ReadQcow2VirtualSize(overlay2),
                IsOkAndHolds(std::optional<uint64_t>(kVirtualSize)));

    // Backing format extension must be "qcow2".
    const std::vector<uint8_t> data = ReadFileBytes(overlay2);
    EXPECT_EQ(ReadBe32(data.data() + 112), 0xE2792ACAU);
    EXPECT_EQ(ReadBe32(data.data() + 116), 5U);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(data.data() + 120), 5), "qcow2");
    EXPECT_THAT(ReadBackingFileFromQcow2(overlay2), IsOkAndHolds(overlay1.string()));
}

TEST(ReadBackingFileFromQcow2Test, ReturnsEmptyStringWhenNoBackingFile) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    ASSERT_THAT(CreateQcow2Overlay(overlay, 1024 * 1024), IsOk());
    EXPECT_THAT(ReadBackingFileFromQcow2(overlay), IsOkAndHolds(""));
}

void WriteBytesFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    WriteFile(path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

void AppendBe16(std::vector<uint8_t>& buf, uint16_t v) {
    const size_t pos = buf.size();
    buf.resize(pos + 2);
    WriteBe16(buf.data() + pos, v);
}

void AppendBe32(std::vector<uint8_t>& buf, uint32_t v) {
    const size_t pos = buf.size();
    buf.resize(pos + 4);
    WriteBe32(buf.data() + pos, v);
}

void AppendBe64(std::vector<uint8_t>& buf, uint64_t v) {
    const size_t pos = buf.size();
    buf.resize(pos + 8);
    WriteBe64(buf.data() + pos, v);
}

TEST(ListQcow2SnapshotsTest, MissingFileReturnsNotFound) {
    TestTempDir temp_dir("qcow2_test");
    EXPECT_THAT(ListQcow2Snapshots(temp_dir.Path() / "nonexistent.qcow2"),
                StatusIs(absl::StatusCode::kNotFound));
}

TEST(ListQcow2SnapshotsTest, NonQcow2FileReturnsInvalidArgument) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path empty_path = temp_dir.Path() / "empty.img";
    WriteFile(empty_path, "");
    EXPECT_THAT(ListQcow2Snapshots(empty_path), StatusIs(absl::StatusCode::kInvalidArgument));

    const fs::path raw_path = temp_dir.Path() / "raw.img";
    WriteFile(raw_path, "raw image contents");
    EXPECT_THAT(ListQcow2Snapshots(raw_path), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ListQcow2SnapshotsTest, TruncatedHeaderReturnsDataLoss) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path truncated_path = temp_dir.Path() / "truncated.qcow2";
    WriteFile(truncated_path, "QFI\xFB");
    EXPECT_THAT(ListQcow2Snapshots(truncated_path), StatusIs(absl::StatusCode::kDataLoss));
}

TEST(ListQcow2SnapshotsTest, UnsupportedVersionReturnsInvalidArgument) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    ASSERT_THAT(CreateQcow2Overlay(overlay, 1024 * 1024), IsOk());

    std::vector<uint8_t> data = ReadFileBytes(overlay);
    WriteBe32(data.data() + 4, 1U);  // version = 1 (unsupported)
    WriteBytesFile(overlay, data);

    EXPECT_THAT(ListQcow2Snapshots(overlay), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ListQcow2SnapshotsTest, ValidOverlayWithNoSnapshotsReturnsEmpty) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    ASSERT_THAT(CreateQcow2Overlay(overlay, 1024 * 1024), IsOk());
    EXPECT_THAT(ListQcow2Snapshots(overlay), IsOkAndHolds(IsEmpty()));
}

TEST(ListQcow2SnapshotsTest, ZeroSnapshotsOffsetWithNonZeroCountReturnsDataLoss) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    ASSERT_THAT(CreateQcow2Overlay(overlay, 1024 * 1024), IsOk());

    std::vector<uint8_t> data = ReadFileBytes(overlay);
    WriteBe32(data.data() + 60, 1U);    // nb_snapshots = 1
    WriteBe64(data.data() + 64, 0ULL);  // snapshots_offset = 0
    WriteBytesFile(overlay, data);

    EXPECT_THAT(ListQcow2Snapshots(overlay), StatusIs(absl::StatusCode::kDataLoss));
}

TEST(ListQcow2SnapshotsTest, TooManySnapshotsReturnsDataLoss) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    ASSERT_THAT(CreateQcow2Overlay(overlay, 1024 * 1024), IsOk());

    std::vector<uint8_t> data = ReadFileBytes(overlay);
    WriteBe32(data.data() + 60, 65537U);  // > QCOW_MAX_SNAPSHOTS (65536)
    WriteBe64(data.data() + 64, data.size());
    WriteBytesFile(overlay, data);

    EXPECT_THAT(ListQcow2Snapshots(overlay), StatusIs(absl::StatusCode::kDataLoss));
}

TEST(ListQcow2SnapshotsTest, SingleSnapshotWithoutExtraData) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    constexpr uint64_t kVirtualSize = 2 * 1024 * 1024;
    ASSERT_THAT(CreateQcow2Overlay(overlay, kVirtualSize), IsOk());

    std::vector<uint8_t> data = ReadFileBytes(overlay);
    const uint64_t snapshots_offset = data.size();
    WriteBe32(data.data() + 60, 1U);
    WriteBe64(data.data() + 64, snapshots_offset);

    constexpr std::string_view kId = "1";
    constexpr std::string_view kName = "default_boot";
    constexpr uint32_t kDateSec = 1700000000U;
    constexpr uint32_t kDateNsec = 500000000U;
    constexpr uint64_t kVmClockNsec = 12345678900ULL;
    constexpr uint32_t kVmStateSize = 4096U;

    AppendBe64(data, 0ULL);  // l1_table_offset
    AppendBe32(data, 0U);    // l1_size
    AppendBe16(data, static_cast<uint16_t>(kId.size()));
    AppendBe16(data, static_cast<uint16_t>(kName.size()));
    AppendBe32(data, kDateSec);
    AppendBe32(data, kDateNsec);
    AppendBe64(data, kVmClockNsec);
    AppendBe32(data, kVmStateSize);
    AppendBe32(data, 0U);  // extra_data_size = 0
    data.insert(data.end(), kId.begin(), kId.end());
    data.insert(data.end(), kName.begin(), kName.end());
    WriteBytesFile(overlay, data);

    EXPECT_THAT(ListQcow2Snapshots(overlay),
                IsOkAndHolds(ElementsAre(Qcow2Snapshot{
                    .id = "1",
                    .name = "default_boot",
                    .vm_state_size = kVmStateSize,
                    .disk_size = kVirtualSize,
                    .date = absl::FromUnixSeconds(kDateSec) + absl::Nanoseconds(kDateNsec),
                    .vm_clock = absl::Nanoseconds(kVmClockNsec),
                    .icount = std::nullopt,
                })));
}

TEST(ListQcow2SnapshotsTest, MultipleSnapshotsWithExtraDataAndAlignment) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    constexpr uint64_t kVirtualSize = 2 * 1024 * 1024;
    ASSERT_THAT(CreateQcow2Overlay(overlay, kVirtualSize), IsOk());

    std::vector<uint8_t> data = ReadFileBytes(overlay);
    const uint64_t snapshots_offset = data.size();
    WriteBe32(data.data() + 60, 2U);
    WriteBe64(data.data() + 64, snapshots_offset);

    // Snapshot 0: 24-byte known extra data, 64-bit vm_state_size_large > 4 GiB, custom disk_size,
    // explicit icount, and unaligned end (40 + 24 + 1 + 8 = 73 bytes).
    constexpr std::string_view kId0 = "1";
    constexpr std::string_view kName0 = "snap_one";
    constexpr uint64_t kLargeVmStateSize = 5ULL * 1024 * 1024 * 1024;
    constexpr uint64_t kCustomDiskSize = 10ULL * 1024 * 1024;
    constexpr uint64_t kIcount0 = 987654321ULL;

    AppendBe64(data, 0ULL);  // l1_table_offset
    AppendBe32(data, 0U);    // l1_size
    AppendBe16(data, static_cast<uint16_t>(kId0.size()));
    AppendBe16(data, static_cast<uint16_t>(kName0.size()));
    AppendBe32(data, 1700000100U);
    AppendBe32(data, 0U);
    AppendBe64(data, 1000000000ULL);
    AppendBe32(data, 123U);  // 32-bit vm_state_size (overridden by vm_state_size_large)
    AppendBe32(data, 24U);   // extra_data_size = 24
    AppendBe64(data, kLargeVmStateSize);
    AppendBe64(data, kCustomDiskSize);
    AppendBe64(data, kIcount0);
    data.insert(data.end(), kId0.begin(), kId0.end());
    data.insert(data.end(), kName0.begin(), kName0.end());

    // Pad to 8-byte boundary for Snapshot 1.
    while (data.size() % 8 != 0) {
        data.push_back(0U);
    }

    // Snapshot 1: 32-byte extra data (24 bytes known with icount = -1ULL + 8 bytes unknown extra).
    constexpr std::string_view kId1 = "2";
    constexpr std::string_view kName1 = "snap_two";
    constexpr uint64_t kVmStateSize1 = 65536ULL;

    AppendBe64(data, 0ULL);  // l1_table_offset
    AppendBe32(data, 0U);    // l1_size
    AppendBe16(data, static_cast<uint16_t>(kId1.size()));
    AppendBe16(data, static_cast<uint16_t>(kName1.size()));
    AppendBe32(data, 1700000200U);
    AppendBe32(data, 250000000U);
    AppendBe64(data, 2500000000ULL);
    AppendBe32(data, 0U);
    AppendBe32(data, 32U);  // extra_data_size = 32 (24 known + 8 unknown)
    AppendBe64(data, kVmStateSize1);
    AppendBe64(data, kVirtualSize);
    AppendBe64(data, std::numeric_limits<uint64_t>::max());  // no icount
    AppendBe64(data, 0xDEADBEEFCAFEBABEULL);                 // unknown extra data
    data.insert(data.end(), kId1.begin(), kId1.end());
    data.insert(data.end(), kName1.begin(), kName1.end());

    WriteBytesFile(overlay, data);

    EXPECT_THAT(
            ListQcow2Snapshots(overlay),
            IsOkAndHolds(ElementsAre(
                    Qcow2Snapshot{
                        .id = "1",
                        .name = "snap_one",
                        .vm_state_size = kLargeVmStateSize,
                        .disk_size = kCustomDiskSize,
                        .date = absl::FromUnixSeconds(1700000100U),
                        .vm_clock = absl::Seconds(1),
                        .icount = kIcount0,
                    },
                    Qcow2Snapshot{
                        .id = "2",
                        .name = "snap_two",
                        .vm_state_size = kVmStateSize1,
                        .disk_size = kVirtualSize,
                        .date = absl::FromUnixSeconds(1700000200U) + absl::Nanoseconds(250000000U),
                        .vm_clock = absl::Milliseconds(2500),
                        .icount = std::nullopt,
                    })));
}

TEST(ListQcow2SnapshotsTest, ExtraDataSizeExceedsMaxReturnsDataLoss) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    ASSERT_THAT(CreateQcow2Overlay(overlay, 1024 * 1024), IsOk());

    std::vector<uint8_t> data = ReadFileBytes(overlay);
    WriteBe32(data.data() + 60, 1U);
    WriteBe64(data.data() + 64, data.size());

    AppendBe64(data, 0ULL);
    AppendBe32(data, 0U);
    AppendBe16(data, 1U);
    AppendBe16(data, 4U);
    AppendBe32(data, 0U);
    AppendBe32(data, 0U);
    AppendBe64(data, 0ULL);
    AppendBe32(data, 0U);
    AppendBe32(data, 1025U);  // > QCOW_MAX_SNAPSHOT_EXTRA_DATA (1024)
    WriteBytesFile(overlay, data);

    EXPECT_THAT(ListQcow2Snapshots(overlay), StatusIs(absl::StatusCode::kDataLoss));
}

TEST(ListQcow2SnapshotsTest, TruncatedSnapshotEntryReturnsDataLoss) {
    TestTempDir temp_dir("qcow2_test");
    const fs::path overlay = temp_dir.Path() / "overlay.qcow2";
    ASSERT_THAT(CreateQcow2Overlay(overlay, 1024 * 1024), IsOk());

    std::vector<uint8_t> data = ReadFileBytes(overlay);
    WriteBe32(data.data() + 60, 1U);
    WriteBe64(data.data() + 64, data.size());

    // Write snapshot header claiming name_size = 10, but only write 3 bytes of name.
    AppendBe64(data, 0ULL);
    AppendBe32(data, 0U);
    AppendBe16(data, 1U);
    AppendBe16(data, 10U);
    AppendBe32(data, 0U);
    AppendBe32(data, 0U);
    AppendBe64(data, 0ULL);
    AppendBe32(data, 0U);
    AppendBe32(data, 0U);
    data.push_back('1');
    data.push_back('a');
    data.push_back('b');
    data.push_back('c');
    WriteBytesFile(overlay, data);

    EXPECT_THAT(ListQcow2Snapshots(overlay), StatusIs(absl::StatusCode::kDataLoss));
}

}  // namespace
}  // namespace goldfish::fsimage
