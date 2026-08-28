// Copyright 2021 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "absl/status/status_matchers.h"

#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/deque_reader.h"
#include "goldfish/archive/deque_writer.h"

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::testing::Not;

namespace goldfish::archive {

TEST(archive, positive) {
    DequeArchive archive;

    constexpr uint32_t kUnsignedNumber = 3000000000U;
    constexpr int32_t kSignedNumber = 2000000000;

    archive << kUnsignedNumber << kSignedNumber << -kSignedNumber;
    EXPECT_FALSE(archive.Empty());

    EXPECT_THAT(ReadOneValue<uint32_t>(archive), IsOkAndHolds(kUnsignedNumber));
    EXPECT_THAT(ReadOneValue<int32_t>(archive), IsOkAndHolds(kSignedNumber));
    EXPECT_THAT(ReadOneValue<int32_t>(archive), IsOkAndHolds(-kSignedNumber));
    EXPECT_TRUE(archive.Empty());
}

TEST(archive, negative) {
    DequeArchive archive;

    EXPECT_THAT(ReadOneValue<int>(archive), Not(IsOk()));
}

TEST(archive, compact_U8) {
    static const uint8_t kCompactUint8Values[] = {0, 127, 128, 255};

    for (const uint8_t x : kCompactUint8Values) {
        DequeArchive archive;
        archive << x;
        EXPECT_EQ(archive.storage.size(), 1);
        EXPECT_THAT(ReadOneValue<uint8_t>(archive), IsOkAndHolds(x));
    }
}

TEST(archive, compact_S8) {
    static const int8_t kCompactInt8Values[] = {0, -127, -128, 127};

    for (const int8_t x : kCompactInt8Values) {
        DequeArchive archive;
        archive << x;
        EXPECT_EQ(archive.storage.size(), 1);
        EXPECT_THAT(ReadOneValue<int8_t>(archive), IsOkAndHolds(x));
    }
}

TEST(archive, compact_BOOL) {
    static const bool kCompactBoolValues[] = {false, true};

    for (const bool x : kCompactBoolValues) {
        DequeArchive archive;
        archive << x;
        EXPECT_EQ(archive.storage.size(), 1);
        EXPECT_THAT(ReadOneValue<bool>(archive), IsOkAndHolds(x));
    }
}

TEST(archive, compact_CHAR) {
    static const char kCompactCharValues[] = {0, 'a', 'z', 127};

    for (const char x : kCompactCharValues) {
        DequeArchive archive;
        archive << x;
        EXPECT_EQ(archive.storage.size(), 1);
        EXPECT_THAT(ReadOneValue<char>(archive), IsOkAndHolds(x));
    }
}

TEST(archive, length_variable) {
    DequeArchive archive;

    // 7bit per byte, rounded up
    constexpr uint32_t kUnsignedNumber1 = (1U << 7) - 1;
    constexpr uint32_t kUnsignedNumber2 = (1U << 10) - 1;
    constexpr uint32_t kUnsignedNumber3 = (1U << 19) - 1;
    constexpr uint32_t kUnsignedNumber4 = (1U << 25) - 1;
    constexpr uint64_t kUnsignedNumber10 = UINT64_MAX;

    archive << kUnsignedNumber1;
    EXPECT_EQ(archive.Size(), 1);
    archive << kUnsignedNumber2;
    EXPECT_EQ(archive.Size(), 1 + 2);
    archive << kUnsignedNumber3;
    EXPECT_EQ(archive.Size(), 1 + 2 + 3);
    archive << kUnsignedNumber4;
    EXPECT_EQ(archive.Size(), 1 + 2 + 3 + 4);
    archive << kUnsignedNumber10;
    EXPECT_EQ(archive.Size(), 1 + 2 + 3 + 4 + 10);
}

TEST(archive, length_fixed) {
    DequeArchive archive;

    constexpr uint8_t kU80 = 0;
    constexpr uint8_t kU8255 = 255;
    constexpr int8_t kI80 = 0;
    constexpr int8_t kI8127 = 127;
    constexpr int8_t kI8M128 = -128;
    constexpr bool kBTrue = true;
    constexpr bool kBFalse = false;
    constexpr char kC0 = 0;
    constexpr char kC127 = 127;
    constexpr char kCM128 = -128;

    archive << kU80 << kU8255 << kI80 << kI8127 << kI8M128 << kBTrue << kBFalse << kC0 << kC127
            << kCM128;

    uint8_t u8_0;
    uint8_t u8_255;
    int8_t i8_0;
    int8_t i8_127;
    int8_t i8_m128;
    bool b_true;
    bool b_false;
    char c_0;
    char c_127;
    char c_m128;

    EXPECT_THAT(ReadValue(archive, u8_0, u8_255, i8_0, i8_127, i8_m128, b_true, b_false, c_0, c_127,
                          c_m128),
                IsOk());

    EXPECT_EQ(u8_0, kU80);
    EXPECT_EQ(u8_255, kU8255);
    EXPECT_EQ(i8_0, kI80);
    EXPECT_EQ(i8_127, kI8127);
    EXPECT_EQ(i8_m128, kI8M128);
    EXPECT_EQ(b_true, kBTrue);
    EXPECT_EQ(b_false, kBFalse);
    EXPECT_EQ(c_0, kC0);
    EXPECT_EQ(c_127, kC127);
    EXPECT_EQ(c_m128, kCM128);
}

TEST(archive, variadic) {
    constexpr uint32_t kUnsignedNumber = 3000000000U;
    constexpr int32_t kSignedNumber = 2000000000;

    DequeArchive archive;

    archive << kUnsignedNumber << kSignedNumber << -kSignedNumber;
    EXPECT_FALSE(archive.Empty());

    uint32_t u;
    int32_t i1;
    int32_t i2;

    EXPECT_THAT(ReadValue(archive, u, i1, i2), IsOk());
    EXPECT_EQ(u, kUnsignedNumber);
    EXPECT_EQ(i1, kSignedNumber);
    EXPECT_EQ(i2, -kSignedNumber);
}

}  // namespace goldfish::archive
