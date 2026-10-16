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

#include "goldfish/fsimage/exfat.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/base/testing/test_temp_dir.h"
#include "goldfish/file/storage_capacity.h"

namespace goldfish::fsimage {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::android::base::StorageCapacity;
using ::android::base::TestTempDir;
using ::android::base::operator""_B;
using ::android::base::operator""_KiB;
using ::android::base::operator""_MiB;

uint16_t ReadLe16(const uint8_t* p) {
    uint16_t v = 0;
    ::memcpy(&v, p, sizeof(v));
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(v);
    }
    return v;
}

uint32_t ReadLe32(const uint8_t* p) {
    uint32_t v = 0;
    ::memcpy(&v, p, sizeof(v));
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(v);
    }
    return v;
}

uint64_t ReadLe64(const uint8_t* p) {
    uint64_t v = 0;
    ::memcpy(&v, p, sizeof(v));
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(v);
    }
    return v;
}

std::vector<uint8_t> ReadBytesAt(const fs::path& path, uint64_t offset, size_t size) {
    std::ifstream ifs(path, std::ios_base::binary);
    EXPECT_TRUE(ifs.good());
    ifs.seekg(static_cast<std::streamoff>(offset));
    std::vector<uint8_t> buf(size, 0U);
    ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
    EXPECT_EQ(static_cast<size_t>(ifs.gcount()), size);
    return buf;
}

TEST(IsExFatFileImageTest, RejectsMissingOrNonExFATFiles) {
    TestTempDir temp_dir("exfat_test");
    EXPECT_FALSE(IsExFatFileImage(temp_dir.Path() / "nonexistent.img"));

    const fs::path small_file = temp_dir.Path() / "small.img";
    {
        std::ofstream ofs(small_file, std::ios_base::binary);
        ofs << "not exfat";
    }
    EXPECT_FALSE(IsExFatFileImage(small_file));
}

TEST(CreateEmptyExFATImageTest, InvalidSizesReturnInvalidArgument) {
    TestTempDir temp_dir("exfat_test");
    const fs::path image = temp_dir.Path() / "sdcard.img";

    EXPECT_THAT(CreateEmptyExFATImage(image, 0_B), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(CreateEmptyExFATImage(image, 513_B), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(CreateEmptyExFATImage(image, 16_KiB), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CreateEmptyExFATImageTest, CreatesValidImageWith4KiBClusters) {
    TestTempDir temp_dir("exfat_test");
    const fs::path image = temp_dir.Path() / "sdcard.img";
    constexpr StorageCapacity kSize = 1_MiB;

    ASSERT_THAT(CreateEmptyExFATImage(image, kSize), IsOk());
    ASSERT_TRUE(fs::exists(image));
    EXPECT_TRUE(IsExFatFileImage(image));
    EXPECT_FALSE(fs::exists(fs::path(image).concat(".tmp")));
    EXPECT_EQ(fs::file_size(image), kSize.Bytes());

    // Read Main Boot Region (12 sectors) and Backup Boot Region (12 sectors).
    const std::vector<uint8_t> main_boot = ReadBytesAt(image, 0, 12 * 512);
    const std::vector<uint8_t> backup_boot = ReadBytesAt(image, 12 * 512, 12 * 512);
    EXPECT_EQ(main_boot, backup_boot);

    // Check Boot Sector fields.
    EXPECT_EQ(main_boot[0], 0xEB);
    EXPECT_EQ(main_boot[1], 0x76);
    EXPECT_EQ(main_boot[2], 0x90);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(main_boot.data() + 3), 8), "EXFAT   ");
    EXPECT_EQ(ReadLe64(main_boot.data() + 72), kSize.Bytes() / 512);  // vol_length
    const uint32_t fat_offset = ReadLe32(main_boot.data() + 80);
    const uint32_t fat_length = ReadLe32(main_boot.data() + 84);
    const uint32_t clu_offset = ReadLe32(main_boot.data() + 88);
    const uint32_t clu_count = ReadLe32(main_boot.data() + 92);
    const uint32_t root_cluster = ReadLe32(main_boot.data() + 96);
    EXPECT_EQ(fat_offset, 24U);
    EXPECT_EQ(fat_length, 2U);
    EXPECT_EQ(clu_offset, 32U);
    EXPECT_EQ(clu_count, 252U);
    EXPECT_EQ(root_cluster, 5U);
    EXPECT_EQ(ReadLe32(main_boot.data() + 100), 1U);       // default vol_serial = 1
    EXPECT_EQ(ReadLe16(main_boot.data() + 104), 0x0100U);  // fs_revision 1.00
    EXPECT_EQ(main_boot[108], 9U);                         // 512 bytes/sector
    EXPECT_EQ(main_boot[109], 3U);                         // 8 sectors/cluster (4 KiB)
    EXPECT_EQ(main_boot[110], 1U);                         // 1 FAT
    EXPECT_EQ(ReadLe16(main_boot.data() + 510), 0xAA55U);

    // Check Extended Boot Sectors (1..8) signatures.
    for (size_t s = 1; s <= 8; ++s) {
        EXPECT_EQ(ReadLe32(main_boot.data() + (s + 1) * 512 - 4), 0xAA550000U);
    }

    // Check FAT entries:
    // FAT[0] = 0xFFFFFFF8, FAT[1] = EOF, FAT[2] = EOF (bitmap),
    // FAT[3] = 4, FAT[4] = EOF (upcase table), FAT[5] = EOF (root dir).
    const std::vector<uint8_t> fat = ReadBytesAt(image, fat_offset * 512ULL, 6 * sizeof(uint32_t));
    EXPECT_EQ(ReadLe32(fat.data() + 0), 0xFFFFFFF8U);
    EXPECT_EQ(ReadLe32(fat.data() + 4), 0xFFFFFFFFU);
    EXPECT_EQ(ReadLe32(fat.data() + 8), 0xFFFFFFFFU);
    EXPECT_EQ(ReadLe32(fat.data() + 12), 4U);
    EXPECT_EQ(ReadLe32(fat.data() + 16), 0xFFFFFFFFU);
    EXPECT_EQ(ReadLe32(fat.data() + 20), 0xFFFFFFFFU);

    // Check Allocation Bitmap at Cluster 2 (4 clusters allocated -> 0x0F).
    const uint64_t heap_byte_offset = clu_offset * 512ULL;
    const std::vector<uint8_t> bitmap = ReadBytesAt(image, heap_byte_offset, 4);
    EXPECT_EQ(bitmap[0], 0x0FU);
    EXPECT_EQ(bitmap[1], 0x00U);

    // Check Root Directory at Cluster 5 (offset = heap_byte_offset + 3 * 4096).
    const std::vector<uint8_t> root_dir = ReadBytesAt(image, heap_byte_offset + 3 * 4096ULL, 96);
    // Entry 0: Volume Label (0x83, char_count = 0)
    EXPECT_EQ(root_dir[0], 0x83U);
    EXPECT_EQ(root_dir[1], 0U);
    // Entry 1: Allocation Bitmap (0x81, start_clu = 2, size = ceil(252 / 8) = 32)
    EXPECT_EQ(root_dir[32], 0x81U);
    EXPECT_EQ(ReadLe32(root_dir.data() + 32 + 20), 2U);
    EXPECT_EQ(ReadLe64(root_dir.data() + 32 + 24), 32ULL);
    // Entry 2: Up-case Table (0x82, checksum = 0xE619D30D, start_clu = 3, size = 5836)
    EXPECT_EQ(root_dir[64], 0x82U);
    EXPECT_EQ(ReadLe32(root_dir.data() + 64 + 4), 0xE619D30DU);
    EXPECT_EQ(ReadLe32(root_dir.data() + 64 + 20), 3U);
    EXPECT_EQ(ReadLe64(root_dir.data() + 64 + 24), 5836ULL);
}

TEST(CreateEmptyExFATImageTest, CreatesValid512MiBImageWith32KiBClusters) {
    TestTempDir temp_dir("exfat_test");
    const fs::path image = temp_dir.Path() / "sdcard_512m.img";
    constexpr StorageCapacity kSize = 512_MiB;

    ASSERT_THAT(CreateEmptyExFATImage(image, kSize), IsOk());
    EXPECT_EQ(fs::file_size(image), kSize.Bytes());

    const std::vector<uint8_t> boot = ReadBytesAt(image, 0, 512);
    EXPECT_EQ(boot[109], 6U);                   // 64 sectors/cluster (32 KiB)
    EXPECT_EQ(ReadLe32(boot.data() + 96), 4U);  // root_cluster = 4 (upcase fits in 1 cluster)
}

TEST(CreateEmptyExFATImageTest, WritesVolumeLabelAndValidatesLengthAndChars) {
    TestTempDir temp_dir("exfat_test");
    const fs::path image = temp_dir.Path() / "sdcard_labeled.img";

    EXPECT_THAT(CreateEmptyExFATImage(image, 1_MiB, {.label = "TOO_LONG_LABEL"}),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(CreateEmptyExFATImage(image, 1_MiB, {.label = "BAD/LABEL"}),
                StatusIs(absl::StatusCode::kInvalidArgument));

    ASSERT_THAT(CreateEmptyExFATImage(image, 1_MiB, {.label = "SDCARD_1234"}), IsOk());

    const std::vector<uint8_t> boot = ReadBytesAt(image, 0, 512);
    const uint32_t clu_offset = ReadLe32(boot.data() + 88);
    const uint64_t root_dir_offset = clu_offset * 512ULL + 3 * 4096ULL;
    const std::vector<uint8_t> label_entry = ReadBytesAt(image, root_dir_offset, 32);

    EXPECT_EQ(label_entry[0], 0x83U);
    EXPECT_EQ(label_entry[1], 11U);
    constexpr std::string_view kExpectedLabel = "SDCARD_1234";
    for (size_t i = 0; i < kExpectedLabel.size(); ++i) {
        EXPECT_EQ(ReadLe16(label_entry.data() + 2 + i * 2),
                  static_cast<uint16_t>(kExpectedLabel[i]));
    }
}

TEST(CreateEmptyExFATImageTest, CleansUpTempFileOnRenameError) {
    TestTempDir temp_dir("exfat_test");
    const fs::path existing_dir = temp_dir.Path() / "dir.img";
    ASSERT_TRUE(fs::create_directory(existing_dir));

    EXPECT_THAT(CreateEmptyExFATImage(existing_dir, 1_MiB), StatusIs(absl::StatusCode::kInternal));
    EXPECT_FALSE(fs::exists(fs::path(existing_dir).concat(".tmp")));
}

void WriteTextFile(const fs::path& path, std::string_view content) {
    std::ofstream ofs(path, std::ios_base::binary | std::ios_base::trunc);
    ASSERT_TRUE(ofs.good());
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    ASSERT_TRUE(ofs.good());
}

TEST(CreateExFATImageFromDirectoryTest, ValidatesSourceDirectory) {
    TestTempDir temp_dir("exfat_test");
    const fs::path image = temp_dir.Path() / "sdcard.img";
    const fs::path missing_dir = temp_dir.Path() / "missing";
    const fs::path regular_file = temp_dir.Path() / "file.txt";
    WriteTextFile(regular_file, "hello");

    EXPECT_THAT(CreateExFATImageFromDirectory(image, missing_dir, 1_MiB),
                StatusIs(absl::StatusCode::kNotFound));
    EXPECT_THAT(CreateExFATImageFromDirectory(image, regular_file, 1_MiB),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CreateExFATImageFromDirectoryTest, PopulatesFilesAndSubdirectories) {
    TestTempDir temp_dir("exfat_test");
    const fs::path src_dir = temp_dir.Path() / "src";
    ASSERT_TRUE(fs::create_directories(src_dir / "subdir" / "empty_nested"));

    WriteTextFile(src_dir / "empty.txt", "");
    constexpr std::string_view kHelloContent = "Hello from exFAT root file!\n";
    WriteTextFile(src_dir / "hello.txt", kHelloContent);
    constexpr std::string_view kLongNameContent =
            "File with a long name spanning multiple FileName directory entries.";
    WriteTextFile(src_dir / "subdir" / "long_filename_spanning_two_dentries.txt", kLongNameContent);

    const fs::path image = temp_dir.Path() / "populated.img";
    ASSERT_THAT(CreateExFATImageFromDirectory(image, src_dir, 1_MiB, {.label = "TEST_VOL"}),
                IsOk());
    EXPECT_EQ(fs::file_size(image), (1_MiB).Bytes());

    const std::vector<uint8_t> boot = ReadBytesAt(image, 0, 512);
    const uint32_t clu_offset = ReadLe32(boot.data() + 88);
    const uint32_t root_cluster = ReadLe32(boot.data() + 96);
    EXPECT_EQ(root_cluster, 5U);

    const auto cluster_to_offset = [clu_offset](uint32_t cluster) -> uint64_t {
        return clu_offset * 512ULL + (cluster - 2U) * 4096ULL;
    };

    // Root directory entries at cluster 5:
    // [0]: Volume Label (0x83)
    // [1]: Allocation Bitmap (0x81)
    // [2]: Up-case Table (0x82)
    // [3..5]: "empty.txt" (File 0x85, StreamExt 0xC0, FileName 0xC1)
    // [6..8]: "hello.txt" (File 0x85, StreamExt 0xC0, FileName 0xC1)
    // [9..11]: "subdir" (File 0x85, StreamExt 0xC0, FileName 0xC1)
    const std::vector<uint8_t> root_dir =
            ReadBytesAt(image, cluster_to_offset(root_cluster), 12 * 32);

    // Check "empty.txt" (0 bytes -> first_cluster = 0, data_length = 0)
    const uint8_t* empty_set = root_dir.data() + 3 * 32;
    EXPECT_EQ(empty_set[0], 0x85U);
    EXPECT_EQ(empty_set[1], 2U);                     // secondary_count = 2
    EXPECT_EQ(ReadLe16(empty_set + 4), 0x0020U);     // Archive attribute
    EXPECT_EQ(empty_set[32], 0xC0U);                 // Stream Extension
    EXPECT_EQ(empty_set[32 + 3], 9U);                // strlen("empty.txt")
    EXPECT_EQ(ReadLe32(empty_set + 32 + 20), 0U);    // first_cluster = 0
    EXPECT_EQ(ReadLe64(empty_set + 32 + 24), 0ULL);  // data_length = 0
    EXPECT_EQ(empty_set[64], 0xC1U);                 // File Name

    // Check "hello.txt"
    const uint8_t* hello_set = root_dir.data() + 6 * 32;
    EXPECT_EQ(hello_set[0], 0x85U);
    EXPECT_EQ(hello_set[32], 0xC0U);
    const uint32_t hello_cluster = ReadLe32(hello_set + 32 + 20);
    const uint64_t hello_size = ReadLe64(hello_set + 32 + 24);
    EXPECT_GT(hello_cluster, 5U);
    EXPECT_EQ(hello_size, kHelloContent.size());
    const std::vector<uint8_t> hello_bytes =
            ReadBytesAt(image, cluster_to_offset(hello_cluster), kHelloContent.size());
    EXPECT_EQ(
            std::string_view(reinterpret_cast<const char*>(hello_bytes.data()), hello_bytes.size()),
            kHelloContent);

    // Check "subdir"
    const uint8_t* subdir_set = root_dir.data() + 9 * 32;
    EXPECT_EQ(subdir_set[0], 0x85U);
    EXPECT_EQ(ReadLe16(subdir_set + 4), 0x0010U);  // Directory attribute
    const uint32_t subdir_cluster = ReadLe32(subdir_set + 32 + 20);
    EXPECT_GT(subdir_cluster, 5U);
    EXPECT_EQ(ReadLe64(subdir_set + 32 + 24), 4096ULL);

    // Check "subdir" entries:
    // [0..2]: "empty_nested" (12 chars -> 1 FileName dentry)
    // [3..7]: "long_filename_spanning_two_dentries.txt" (39 chars -> 3 FileName dentries)
    const std::vector<uint8_t> subdir_bytes =
            ReadBytesAt(image, cluster_to_offset(subdir_cluster), 8 * 32);
    const uint8_t* long_set = subdir_bytes.data() + 3 * 32;
    EXPECT_EQ(long_set[0], 0x85U);
    EXPECT_EQ(long_set[1], 4U);  // 1 StreamExt + 3 FileName dentries
    const uint32_t long_cluster = ReadLe32(long_set + 32 + 20);
    const uint64_t long_size = ReadLe64(long_set + 32 + 24);
    EXPECT_EQ(long_size, kLongNameContent.size());
    const std::vector<uint8_t> long_file_bytes =
            ReadBytesAt(image, cluster_to_offset(long_cluster), kLongNameContent.size());
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(long_file_bytes.data()),
                               long_file_bytes.size()),
              kLongNameContent);
}

TEST(CreateExFATImageFromDirectoryTest, ReturnsErrorWhenImageTooSmallForFiles) {
    TestTempDir temp_dir("exfat_test");
    const fs::path src_dir = temp_dir.Path() / "src";
    ASSERT_TRUE(fs::create_directory(src_dir));
    const std::string large_data(600 * 1024, 'A');
    WriteTextFile(src_dir / "large.bin", large_data);

    const fs::path image = temp_dir.Path() / "small.img";
    EXPECT_THAT(CreateExFATImageFromDirectory(image, src_dir, 512_KiB),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CreateExFATImageFromDirectoryTest, SortsDirectoryEntriesByU8String) {
    TestTempDir temp_dir("exfat_test");
    const fs::path src_dir = temp_dir.Path() / "src";
    ASSERT_TRUE(fs::create_directory(src_dir));

    // Create files out of order; UTF-8 code-unit order is '0' (0x30) < 'A' (0x41) < '_' (0x5F) <
    // 'b' (0x62) < 'z' (0x7A).
    for (std::string_view name : {"z.txt", "b.txt", "_.txt", "A.txt", "0.txt"}) {
        WriteTextFile(src_dir / name, name);
    }

    const fs::path image = temp_dir.Path() / "sorted.img";
    ASSERT_THAT(CreateExFATImageFromDirectory(image, src_dir, 1_MiB), IsOk());

    const std::vector<uint8_t> boot = ReadBytesAt(image, 0, 512);
    const uint32_t clu_offset = ReadLe32(boot.data() + 88);
    const uint32_t root_cluster = ReadLe32(boot.data() + 96);
    const uint64_t root_dir_offset = clu_offset * 512ULL + (root_cluster - 2U) * 4096ULL;

    // First 3 root dentries are Volume Label, Bitmap, Up-case Table.
    // Each short filename uses 3 dentries: File (0x85), StreamExt (0xC0), FileName (0xC1).
    const std::vector<uint8_t> root_dir = ReadBytesAt(image, root_dir_offset, (3 + 5 * 3) * 32ULL);
    constexpr std::array<char, 5> kExpectedFirstChars = {'0', 'A', '_', 'b', 'z'};
    for (size_t i = 0; i < kExpectedFirstChars.size(); ++i) {
        const uint8_t* set_base = root_dir.data() + (3 + i * 3) * 32;
        EXPECT_EQ(set_base[0], 0x85U);
        EXPECT_EQ(set_base[64], 0xC1U);
        EXPECT_EQ(ReadLe16(set_base + 64 + 2), static_cast<uint16_t>(kExpectedFirstChars[i]));
    }
}

TEST(CreateEmptyExFATImageTest, WritesCustomSerialNumber) {
    TestTempDir temp_dir("exfat_test");
    const fs::path image = temp_dir.Path() / "serial.img";

    ASSERT_THAT(CreateEmptyExFATImage(image, 1_MiB, {.serial_number = 0xCAFEBABEU}), IsOk());

    const std::vector<uint8_t> boot = ReadBytesAt(image, 0, 512);
    EXPECT_EQ(ReadLe32(boot.data() + 100), 0xCAFEBABEU);
}

TEST(CreateEmptyExFATImageTest, RejectsTimestampOutsideExfatYearRange) {
    TestTempDir temp_dir("exfat_test");
    const fs::path image = temp_dir.Path() / "invalid_time.img";

    const absl::Time before_1980 =
            absl::FromCivil(absl::CivilSecond(1979, 12, 31, 23, 59, 59), absl::UTCTimeZone());
    const absl::Time after_2107 =
            absl::FromCivil(absl::CivilSecond(2108, 1, 1, 0, 0, 0), absl::UTCTimeZone());

    EXPECT_THAT(CreateEmptyExFATImage(image, 1_MiB, {.timestamp = before_1980}),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(CreateEmptyExFATImage(image, 1_MiB, {.timestamp = after_2107}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CreateExFATImageFromDirectoryTest, EncodesCustomTimestampAndProducesIdenticalImages) {
    TestTempDir temp_dir("exfat_test");
    const fs::path src_dir = temp_dir.Path() / "src";
    ASSERT_TRUE(fs::create_directories(src_dir / "nested"));
    WriteTextFile(src_dir / "alpha.txt", "alpha");
    WriteTextFile(src_dir / "nested" / "beta.txt", "beta");

    const absl::Time fixed_time =
            absl::FromCivil(absl::CivilSecond(2026, 7, 4, 15, 30, 45), absl::UTCTimeZone()) +
            absl::Milliseconds(670);
    const ExfatVolumeAttributes attrs = {
        .label = "REPRO",
        .serial_number = 0x13572468U,
        .timestamp = fixed_time,
    };

    const fs::path image1 = temp_dir.Path() / "repro1.img";
    const fs::path image2 = temp_dir.Path() / "repro2.img";
    ASSERT_THAT(CreateExFATImageFromDirectory(image1, src_dir, 1_MiB, attrs), IsOk());
    ASSERT_THAT(CreateExFATImageFromDirectory(image2, src_dir, 1_MiB, attrs), IsOk());

    const std::vector<uint8_t> bytes1 = ReadBytesAt(image1, 0, (1_MiB).Bytes());
    const std::vector<uint8_t> bytes2 = ReadBytesAt(image2, 0, (1_MiB).Bytes());
    EXPECT_EQ(bytes1, bytes2);

    const std::vector<uint8_t> boot = ReadBytesAt(image1, 0, 512);
    EXPECT_EQ(ReadLe32(boot.data() + 100), 0x13572468U);
    const uint32_t clu_offset = ReadLe32(boot.data() + 88);
    const uint32_t root_cluster = ReadLe32(boot.data() + 96);
    const uint64_t root_dir_offset = clu_offset * 512ULL + (root_cluster - 2U) * 4096ULL;

    // Entry 3 in root directory is the File directory entry (0x85) for "alpha.txt".
    const std::vector<uint8_t> file_dentry = ReadBytesAt(image1, root_dir_offset + 3 * 32ULL, 32);
    EXPECT_EQ(file_dentry[0], 0x85U);
    constexpr uint32_t kExpectedDosTimestamp =
            ((2026U - 1980U) << 25U) | (7U << 21U) | (4U << 16U) | (15U << 11U) | (30U << 5U) | 22U;
    EXPECT_EQ(ReadLe32(file_dentry.data() + 8), kExpectedDosTimestamp);   // create_timestamp
    EXPECT_EQ(ReadLe32(file_dentry.data() + 12), kExpectedDosTimestamp);  // last_modified_timestamp
    EXPECT_EQ(ReadLe32(file_dentry.data() + 16), kExpectedDosTimestamp);  // last_accessed_timestamp
    EXPECT_EQ(file_dentry[20], 167U);   // create_10ms_increment: (45 % 2) * 100 + 67
    EXPECT_EQ(file_dentry[21], 167U);   // last_modified_10ms_increment
    EXPECT_EQ(file_dentry[22], 0x80U);  // create_utc_offset: valid UTC+0
    EXPECT_EQ(file_dentry[23], 0x80U);  // last_modified_utc_offset
    EXPECT_EQ(file_dentry[24], 0x80U);  // last_accessed_utc_offset
}

TEST(CreateExFATImageFromDirectoryTest, UsesCurrentTimeByDefault) {
    TestTempDir temp_dir("exfat_test");
    const fs::path src_dir = temp_dir.Path() / "src";
    ASSERT_TRUE(fs::create_directory(src_dir));
    WriteTextFile(src_dir / "file.txt", "data");

    const absl::Time before = absl::Now();
    const fs::path image = temp_dir.Path() / "default_time.img";
    ASSERT_THAT(CreateExFATImageFromDirectory(image, src_dir, 1_MiB), IsOk());

    const std::vector<uint8_t> boot = ReadBytesAt(image, 0, 512);
    const uint32_t clu_offset = ReadLe32(boot.data() + 88);
    const uint32_t root_cluster = ReadLe32(boot.data() + 96);
    const uint64_t root_dir_offset = clu_offset * 512ULL + (root_cluster - 2U) * 4096ULL;
    const std::vector<uint8_t> file_dentry = ReadBytesAt(image, root_dir_offset + 3 * 32ULL, 32);

    const uint32_t dos_timestamp = ReadLe32(file_dentry.data() + 8);
    const int encoded_year = 1980 + static_cast<int>(dos_timestamp >> 25U);
    const int min_expected_year = static_cast<int>(absl::UTCTimeZone().At(before).cs.year());
    EXPECT_GE(encoded_year, min_expected_year);
    EXPECT_EQ(file_dentry[22], 0x80U);
}

TEST(CreateExFATImageFromDirectoryTest, SizesImageToFitDirectoryWhenSizeIsZero) {
    TestTempDir temp_dir("exfat_test");
    const fs::path src_dir = temp_dir.Path() / "src";
    ASSERT_TRUE(fs::create_directories(src_dir / "nested"));
    const std::string payload(10000, 'X');
    WriteTextFile(src_dir / "file_a.bin", payload);
    WriteTextFile(src_dir / "nested" / "file_b.txt", "nested_text");

    const fs::path image = temp_dir.Path() / "autosized.img";
    ASSERT_THAT(CreateExFATImageFromDirectory(image, src_dir, 0_B), IsOk());
    EXPECT_TRUE(IsExFatFileImage(image));

    const uint64_t actual_bytes = fs::file_size(image);
    EXPECT_GT(actual_bytes, 0ULL);
    EXPECT_EQ(actual_bytes % 4096ULL, 0ULL);

    const std::vector<uint8_t> boot = ReadBytesAt(image, 0, 512);
    EXPECT_EQ(ReadLe64(boot.data() + 72) * 512ULL, actual_bytes);  // vol_length * 512
    EXPECT_EQ(boot[112], 100U);                                    // percent_in_use == 100%

    // Reducing capacity by one cluster (4 KiB) must fail as too small.
    const fs::path too_small_image = temp_dir.Path() / "too_small.img";
    EXPECT_THAT(CreateExFATImageFromDirectory(too_small_image, src_dir,
                                              StorageCapacity(actual_bytes - 4096ULL)),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CreateExFATImageFromDirectoryTest, SizesImageToFitEmptyDirectoryWhenSizeIsZero) {
    TestTempDir temp_dir("exfat_test");
    const fs::path src_dir = temp_dir.Path() / "empty_src";
    ASSERT_TRUE(fs::create_directory(src_dir));

    const fs::path image = temp_dir.Path() / "empty_autosized.img";
    ASSERT_THAT(CreateExFATImageFromDirectory(image, src_dir, 0_B), IsOk());
    EXPECT_TRUE(IsExFatFileImage(image));
    EXPECT_EQ(fs::file_size(image), 32768ULL);
}

}  // namespace
}  // namespace goldfish::fsimage
