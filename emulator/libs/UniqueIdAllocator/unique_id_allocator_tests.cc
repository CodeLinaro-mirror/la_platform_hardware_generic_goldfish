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

#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/deque_reader.h"
#include "goldfish/archive/deque_writer.h"
#include "goldfish/unique_id_allocator.h"

using goldfish::UniqueIdAllocator;
using goldfish::archive::DequeArchive;
using goldfish::archive::DequeReader;
using goldfish::archive::DequeWriter;

TEST(UniqueIdAllocator, getput_all) {
    UniqueIdAllocator allocator;

    EXPECT_EQ(allocator.get(), 1);
    EXPECT_EQ(allocator.get(), 2);
    EXPECT_EQ(allocator.get(), 3);
    EXPECT_EQ(allocator.get(), 4);
    EXPECT_EQ(allocator.get(), 5);

    allocator.put(4);
    allocator.put(1);
    allocator.put(2);
    allocator.put(5);
    allocator.put(3);

    EXPECT_EQ(allocator.get(), 1);
}

TEST(UniqueIdAllocator, getput_some) {
    UniqueIdAllocator allocator;

    EXPECT_EQ(allocator.get(), 1);
    EXPECT_EQ(allocator.get(), 2);
    EXPECT_EQ(allocator.get(), 3);
    EXPECT_EQ(allocator.get(), 4);
    EXPECT_EQ(allocator.get(), 5);

    allocator.put(2);
    allocator.put(3);
    EXPECT_EQ(allocator.get(), 2);
    EXPECT_EQ(allocator.get(), 3);

    EXPECT_EQ(allocator.get(), 6);
}

TEST(UniqueIdAllocator, snapshot) {
    DequeArchive archive;

    {
        UniqueIdAllocator allocator;

        EXPECT_EQ(allocator.get(), 1);
        EXPECT_EQ(allocator.get(), 2);
        EXPECT_EQ(allocator.get(), 3);
        EXPECT_EQ(allocator.get(), 4);
        EXPECT_EQ(allocator.get(), 5);
        allocator.put(2);

        allocator.saveToSnapshot(archive);
    }

    {
        UniqueIdAllocator allocator;
        allocator.loadFromSnapshot(archive);

        EXPECT_EQ(allocator.get(), 2);
        EXPECT_EQ(allocator.get(), 6);
    }
}
