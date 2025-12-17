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

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/deque_reader.h"
#include "goldfish/archive/deque_writer.h"

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
    EXPECT_EQ(GetString(reader), str);
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
    EXPECT_EQ(GetUnsigned(archive), kUnsignedNumber);
    EXPECT_EQ(GetString(archive), string1);
    EXPECT_EQ(GetSigned(archive), kSignedNumber);
    EXPECT_EQ(GetString(archive), string2);
    EXPECT_EQ(GetSigned(archive), -kSignedNumber);
    EXPECT_TRUE(archive.Empty());
}

TEST(archive, negative) {
    const std::string string1 = "Android Studio Emulator";

    DequeArchive archive;

    archive << string1;
    EXPECT_FALSE(archive.Empty());
    archive.storage.pop_back();
    EXPECT_EQ(GetString(archive), "");
    EXPECT_TRUE(archive.Empty());
}

TEST(archive, length) {
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
