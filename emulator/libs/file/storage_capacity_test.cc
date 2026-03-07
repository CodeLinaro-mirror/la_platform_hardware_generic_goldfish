
// Copyright (C) 2024 The Android Open Source Project
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
#include "goldfish/file/storage_capacity.h"

#include <gtest/gtest.h>

#include <string_view>

#include "gtest/gtest.h"

using namespace android::base;

TEST(StorageCapacityTest, ParseValidStrings) {
    // Test parsing valid storage capacity strings
    absl::StatusOr<StorageCapacity> result = StorageCapacity::Parse("1024");
    absl::StatusOr<StorageCapacity> result_kb = StorageCapacity::Parse("1024kb");
    absl::StatusOr<StorageCapacity> result_mb = StorageCapacity::Parse("2mb");
    absl::StatusOr<StorageCapacity> result_gb = StorageCapacity::Parse("4Gb");
    absl::StatusOr<StorageCapacity> result_tb = StorageCapacity::Parse("4Tb");

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result_kb.ok());
    ASSERT_TRUE(result_mb.ok());
    ASSERT_TRUE(result_gb.ok());
    ASSERT_TRUE(result_tb.ok());

    EXPECT_EQ(result.value().Bytes(), 1024ULL);
    EXPECT_EQ(result_kb.value().Bytes(), 1024ULL * 1024ULL);
    EXPECT_EQ(result_mb.value().Bytes(), 2 * 1024ULL * 1024ULL);
    EXPECT_EQ(result_gb.value().Bytes(), 4 * 1024ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(result_tb.value().Bytes(), 4 * 1024ULL * 1024ULL * 1024ULL * 1024ULL);
}

TEST(StorageCapacityTest, ParseValidStringViewWithoutNullTerminate) {
    // Test parsing valid storage capacity string view without null terminator
    char str[] = {'8', 'k', 'b'};

    absl::StatusOr<StorageCapacity> result =
            StorageCapacity::Parse(std::string_view(str, sizeof(str)));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().Bytes(), 8ULL * 1024ULL);
}

TEST(StorageCapacityTest, ParseInvalidStrings) {
    // Test parsing invalid storage capacity strings
    absl::StatusOr<StorageCapacity> result_invalid = StorageCapacity::Parse("invalid");

    ASSERT_FALSE(result_invalid.ok());
    EXPECT_EQ(result_invalid.status().code(), absl::StatusCode::kInvalidArgument);
}

// Test cases for the string() method
TEST(StorageCapacityTest, StringRepresentation) {
    // Test cases with different storage capacities
    EXPECT_EQ(StorageCapacity(1024).String(), "1.00 KiB");
    EXPECT_EQ(StorageCapacity(1024 * 1024).String(), "1.00 MiB");
    EXPECT_EQ(StorageCapacity(1024 * 1024 * 1024).String(), "1.00 GiB");
    EXPECT_EQ(StorageCapacity(2048).String(), "2.00 KiB");
    EXPECT_EQ(StorageCapacity(1, StorageCapacity::Unit::kTiB).String(), "1.00 TiB");
}

TEST(StorageCapacityTest, AlignMethod) {
    // Test cases with different alignments
    StorageCapacity capacity1(1500);
    StorageCapacity alignment1(1024);
    EXPECT_EQ(capacity1.Align(alignment1).Bytes(),
              2048ULL);  // 1500 rounded up to the nearest 1024

    StorageCapacity capacity2(3000);
    StorageCapacity alignment2(2048);
    EXPECT_EQ(capacity2.Align(alignment2).Bytes(),
              4096ULL);  // 3000 rounded up to the nearest 2048

    StorageCapacity capacity3(5000);
    StorageCapacity alignment3(4096);
    EXPECT_EQ(capacity3.Align(alignment3).Bytes(),
              8192ULL);  // 5000 rounded up to the nearest 4096
}

TEST(StorageCapacityTest, UserDefinedLiterals) {
    // Test user-defined literals
    StorageCapacity a = 8_B;
    StorageCapacity x = 1024_KiB;
    StorageCapacity y = 2_MiB;
    StorageCapacity z = 4_GiB;

    EXPECT_EQ(a.Bytes(), 8);
    EXPECT_EQ(x.Bytes(), 1024ULL * 1024ULL);
    EXPECT_EQ(y.Bytes(), 2ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(z.Bytes(), 4ULL * 1024ULL * 1024ULL * 1024ULL);
}

TEST(StorageCapacityTest, ComparisonOperators) {
    StorageCapacity x = 1_KiB;
    StorageCapacity y = 1_MiB;
    StorageCapacity z = 2_MiB;

    // Test equality
    EXPECT_EQ(x, 1024ULL);
    EXPECT_EQ(y, 1024 * 1024);
    EXPECT_EQ(z, 2 * 1024 * 1024);

    // Test inequality
    EXPECT_NE(x, y);
    EXPECT_NE(y, z);
    EXPECT_NE(x, z);

    // Test less than
    EXPECT_LT(x, y);
    EXPECT_LT(x, z);
    EXPECT_LT(y, z);

    // Test less than or equal to
    EXPECT_LE(x, y);
    EXPECT_LE(x, z);
    EXPECT_LE(y, z);
    EXPECT_LE(y, y);

    // Test greater than
    EXPECT_GT(y, x);
    EXPECT_GT(z, x);
    EXPECT_GT(z, y);

    // Test greater than or equal to
    EXPECT_GE(y, x);
    EXPECT_GE(z, x);
    EXPECT_GE(z, y);
    EXPECT_GE(y, y);
}

TEST(StorageCapacityTest, InvalidNumberErrorMessage) {
    // Test parsing invalid number error message
    absl::StatusOr<StorageCapacity> result = StorageCapacity::Parse("abc");

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(), "Invalid number: abc");
}

TEST(StorageCapacityTest, NegativeNumberErrorMessage) {
    // Test parsing invalid number error message
    absl::StatusOr<StorageCapacity> result = StorageCapacity::Parse("-20");

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(), "Invalid number: -20");
}

TEST(StorageCapacityTest, NumberOutOfRangeErrorMessage) {
    // Test parsing number out of range error message
    absl::StatusOr<StorageCapacity> result =
            StorageCapacity::Parse("99999999999999999999999999999999999999");

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(),
              "Number out of range: 99999999999999999999999999999999999999");
}

TEST(StorageCapacityTest, UnknownLabelErrorMessage) {
    // Test parsing unknown label error message
    absl::StatusOr<StorageCapacity> result = StorageCapacity::Parse("123X");

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(), "Unknown label in: 123X");
}
