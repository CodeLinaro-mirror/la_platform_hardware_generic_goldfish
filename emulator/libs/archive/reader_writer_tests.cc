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

using goldfish::archive::DequeArchive;
using goldfish::archive::DequeReader;
using goldfish::archive::DequeWriter;

TEST(archive, example) {
    DequeWriter::Storage storage;
    DequeWriter writer(&storage);
    DequeReader reader(&storage);

    const std::string str = "Hello, world!";

    writer << str;
    EXPECT_FALSE(storage.empty());
    EXPECT_THAT(ReadValue<std::string>(reader), IsOkAndHolds(str));
    EXPECT_TRUE(storage.empty());
}

TEST(archive, positive) {
    const std::string string1 = "Android Studio Emulator";
    const std::string string2 = "QEMU";

    DequeArchive archive;

    constexpr uint32_t kUnsignedNumber = 3000000000U;
    constexpr int32_t kSignedNumber = 2000000000;

    archive << kUnsignedNumber << string1 << kSignedNumber << string2 << -kSignedNumber;
    EXPECT_FALSE(archive.Empty());

    EXPECT_THAT(ReadValue<uint32_t>(archive), IsOkAndHolds(kUnsignedNumber));
    EXPECT_THAT(ReadValue<std::string>(archive), IsOkAndHolds(string1));
    EXPECT_THAT(ReadValue<int32_t>(archive), IsOkAndHolds(kSignedNumber));
    EXPECT_THAT(ReadValue<std::string>(archive), IsOkAndHolds(string2));
    EXPECT_THAT(ReadValue<int32_t>(archive), IsOkAndHolds(-kSignedNumber));
    EXPECT_TRUE(archive.Empty());
}

TEST(archive, negative) {
    const std::string string1 = "Android Studio Emulator";

    DequeArchive archive;

    archive << string1;
    EXPECT_FALSE(archive.Empty());
    archive.storage.pop_back();
    EXPECT_THAT(ReadValue<std::string>(archive), Not(IsOk()));
}

TEST(archive, compact_U8) {
    static const uint8_t a[] = {0, 127, 128, 255};

    for (const uint8_t x : a) {
        DequeArchive archive;
        archive << x;
        EXPECT_EQ(archive.storage.size(), 1);
        EXPECT_THAT(ReadValue<uint8_t>(archive), IsOkAndHolds(x));
    }
}

TEST(archive, compact_S8) {
    static const int8_t a[] = {0, -127, -128, 127};

    for (const int8_t x : a) {
        DequeArchive archive;
        archive << x;
        EXPECT_EQ(archive.storage.size(), 1);
        EXPECT_THAT(ReadValue<int8_t>(archive), IsOkAndHolds(x));
    }
}

TEST(archive, compact_BOOL) {
    static const bool a[] = {false, true};

    for (const bool x : a) {
        DequeArchive archive;
        archive << x;
        EXPECT_EQ(archive.storage.size(), 1);
        EXPECT_THAT(ReadValue<bool>(archive), IsOkAndHolds(x));
    }
}

TEST(archive, compact_CHAR) {
    static const char a[] = {0, 'a', 'z', 127};

    for (const char x : a) {
        DequeArchive archive;
        archive << x;
        EXPECT_EQ(archive.storage.size(), 1);
        EXPECT_THAT(ReadValue<char>(archive), IsOkAndHolds(x));
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

    constexpr uint8_t kU8_0 = 0;
    constexpr uint8_t kU8_255 = 255;
    constexpr int8_t kI8_0 = 0;
    constexpr int8_t kI8_127 = 127;
    constexpr int8_t kI8_m128 = -128;
    constexpr bool kB_true = true;
    constexpr bool kB_false = false;
    constexpr char kC_0 = 0;
    constexpr char kC_127 = 127;
    constexpr char kC_m128 = -128;

    archive << kU8_0 << kU8_255 << kI8_0 << kI8_127 << kI8_m128 << kB_true << kB_false << kC_0
            << kC_127 << kC_m128;

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

    EXPECT_EQ(u8_0, kU8_0);
    EXPECT_EQ(u8_255, kU8_255);
    EXPECT_EQ(i8_0, kI8_0);
    EXPECT_EQ(i8_127, kI8_127);
    EXPECT_EQ(i8_m128, kI8_m128);
    EXPECT_EQ(b_true, kB_true);
    EXPECT_EQ(b_false, kB_false);
    EXPECT_EQ(c_0, kC_0);
    EXPECT_EQ(c_127, kC_127);
    EXPECT_EQ(c_m128, kC_m128);
}

TEST(archive, variadic_positive) {
    const std::string string1 = "Android Studio Emulator";
    const std::string string2 = "QEMU";
    constexpr uint32_t kUnsignedNumber = 3000000000U;
    constexpr int32_t kSignedNumber = 2000000000;

    DequeArchive archive;

    archive << kUnsignedNumber << string1 << kSignedNumber << string2 << -kSignedNumber;
    EXPECT_FALSE(archive.Empty());

    uint32_t u;
    std::string s1;
    std::string s2;
    int32_t i1;
    int32_t i2;

    EXPECT_THAT(ReadValue(archive, u, s1, i1, s2, i2), IsOk());
    EXPECT_EQ(u, kUnsignedNumber);
    EXPECT_EQ(s1, string1);
    EXPECT_EQ(i1, kSignedNumber);
    EXPECT_EQ(s2, string2);
    EXPECT_EQ(i2, -kSignedNumber);
}

TEST(archive, variadic_negative) {
    const std::string string1 = "Android Studio Emulator";
    const std::string string2 = "QEMU";
    constexpr uint32_t kUnsignedNumber = 3000000000U;
    constexpr int32_t kSignedNumber = 2000000000;

    DequeArchive archive;

    archive << kUnsignedNumber << string1 << kSignedNumber << string2 << -kSignedNumber;
    EXPECT_FALSE(archive.Empty());
    archive.storage.pop_back();

    uint32_t u;
    std::string s1;
    std::string s2;
    int32_t i1;
    int32_t i2;

    EXPECT_THAT(ReadValue(archive, u, s1, i1, s2, i2), Not(IsOk()));
}
