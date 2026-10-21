/* Copyright (C) 2025 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"

#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/deque_reader.h"
#include "goldfish/archive/deque_writer.h"
#include "goldfish/unique_id_allocator.h"

using ::absl_testing::IsOk;
using ::testing::Not;

using goldfish::UniqueIdAllocator;
using goldfish::archive::DequeArchive;
using goldfish::archive::DequeReader;
using goldfish::archive::DequeWriter;

TEST(UniqueIdAllocator, getput_all) {
    UniqueIdAllocator allocator;

    EXPECT_EQ(allocator.Get(), 1);
    EXPECT_EQ(allocator.Get(), 2);
    EXPECT_EQ(allocator.Get(), 3);
    EXPECT_EQ(allocator.Get(), 4);
    EXPECT_EQ(allocator.Get(), 5);

    allocator.Put(4);
    allocator.Put(1);
    allocator.Put(2);
    allocator.Put(5);
    allocator.Put(3);

    EXPECT_EQ(allocator.Get(), 1);
}

TEST(UniqueIdAllocator, getput_some) {
    UniqueIdAllocator allocator;

    EXPECT_EQ(allocator.Get(), 1);
    EXPECT_EQ(allocator.Get(), 2);
    EXPECT_EQ(allocator.Get(), 3);
    EXPECT_EQ(allocator.Get(), 4);
    EXPECT_EQ(allocator.Get(), 5);

    allocator.Put(2);
    allocator.Put(3);
    EXPECT_EQ(allocator.Get(), 2);
    EXPECT_EQ(allocator.Get(), 3);

    EXPECT_EQ(allocator.Get(), 6);
}

TEST(UniqueIdAllocator, snapshot) {
    DequeArchive archive;

    {
        UniqueIdAllocator allocator;

        EXPECT_EQ(allocator.Get(), 1);
        EXPECT_EQ(allocator.Get(), 2);
        EXPECT_EQ(allocator.Get(), 3);
        EXPECT_EQ(allocator.Get(), 4);
        EXPECT_EQ(allocator.Get(), 5);
        allocator.Put(2);

        archive << allocator;
    }

    {
        UniqueIdAllocator allocator;
        ASSERT_THAT(ReadValue(archive, allocator), IsOk());

        EXPECT_EQ(allocator.Get(), 2);
        EXPECT_EQ(allocator.Get(), 6);
    }
}

TEST(UniqueIdAllocator, snapshot_negative) {
    // 1. Returned ID >= last_id_
    {
        DequeArchive archive;
        archive << uint32_t{3} << size_t{1} << uint32_t{3};

        UniqueIdAllocator allocator;
        EXPECT_THAT(ReadValue(archive, allocator), Not(IsOk()));
    }

    // 2. Returned ID == kEmptyId (0)
    {
        DequeArchive archive;
        archive << uint32_t{3} << size_t{1} << uint32_t{0};

        UniqueIdAllocator allocator;
        EXPECT_THAT(ReadValue(archive, allocator), Not(IsOk()));
    }
}
