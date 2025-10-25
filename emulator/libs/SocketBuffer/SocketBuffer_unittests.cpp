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

#include <cstring>

#include "goldfish/SocketBuffer.h"
#include "goldfish/archive/DequeArchive.h"
#include "goldfish/archive/DequeReader.h"
#include "goldfish/archive/DequeWriter.h"

using goldfish::SocketBuffer;
using goldfish::archive::DequeArchive;
using goldfish::archive::DequeReader;
using goldfish::archive::DequeWriter;

TEST(SocketBuffer, append_size) {
    SocketBuffer buffer;
    EXPECT_EQ(buffer.append("test", 1), 1);
    EXPECT_EQ(buffer.size(), 1);

    EXPECT_EQ(buffer.append("test", 2), 1 + 2);
    EXPECT_EQ(buffer.size(), 1 + 2);

    EXPECT_EQ(buffer.append("test", 3), 1 + 2 + 3);
    EXPECT_EQ(buffer.size(), 1 + 2 + 3);

    EXPECT_EQ(buffer.append("test", 4), 1 + 2 + 3 + 4);
    EXPECT_EQ(buffer.size(), 1 + 2 + 3 + 4);
}

TEST(SocketBuffer, append_empty_zero) {
    SocketBuffer buffer;
    EXPECT_EQ(buffer.append("test", 0), 0);
    EXPECT_EQ(buffer.append("test", 0), 0);
    EXPECT_EQ(buffer.peek().second, 0);
    EXPECT_EQ(buffer.capacity(), 0);
}

TEST(SocketBuffer, consume_empty_zero) {
    SocketBuffer buffer;
    buffer.consume(0);  // does not crash
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.peek().second, 0);
    EXPECT_EQ(buffer.capacity(), 0);
}

TEST(SocketBuffer, clear) {
    SocketBuffer buffer;

    EXPECT_EQ(buffer.append("test", 4), 4);
    const size_t capacity = buffer.capacity();
    EXPECT_GE(capacity, buffer.size());

    buffer.clear(false);
    EXPECT_EQ(buffer.capacity(), capacity);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.peek().second, 0);

    buffer.clear(true);
    EXPECT_EQ(buffer.capacity(), 0);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.peek().second, 0);
}

TEST(SocketBuffer, append_peek_consume) {
    SocketBuffer buffer;

    EXPECT_EQ(buffer.append("Hello", 5), 5);
    EXPECT_EQ(buffer.append(", world!", 8), 5 + 8);

    const size_t capacity = buffer.capacity();
    EXPECT_GE(capacity, buffer.size());
    EXPECT_LT(capacity, SocketBuffer::kLargeCapacityReleaseIfEmpty);  // see release_large_buffer

    {
        DequeArchive archive;
        buffer.saveToSnapshot(archive);
        SocketBuffer loaded;
        EXPECT_EQ(loaded.loadFromSnapshot(archive), 0);  // this will flatten it
        EXPECT_EQ(loaded.size(), 13);

        const auto [ptr, len] = loaded.peek();
        EXPECT_EQ(len, 13);
        EXPECT_EQ(::strncmp(static_cast<const char*>(ptr), "Hello, world!", 13), 0);
    }

    EXPECT_EQ(buffer.consume(7), 5 + 8 - 7);

    {
        DequeArchive archive;
        buffer.saveToSnapshot(archive);
        SocketBuffer loaded;
        EXPECT_EQ(loaded.loadFromSnapshot(archive), 0);  // this will flatten it
        EXPECT_EQ(loaded.size(), 6);

        const auto [ptr, len] = loaded.peek();
        EXPECT_EQ(len, 6);
        EXPECT_EQ(::strncmp(static_cast<const char*>(ptr), "world!", 6), 0);
    }

    EXPECT_EQ(buffer.consume(6), 5 + 8 - 7 - 6);

    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.peek().second, 0);
    EXPECT_EQ(buffer.capacity(), capacity);  // see release_large_buffer
}

TEST(SocketBuffer, release_large_buffer) {
    SocketBuffer buffer;

    {
        std::vector<char> data(SocketBuffer::kLargeCapacityReleaseIfEmpty);
        EXPECT_EQ(buffer.append(data.data(), data.size()), data.size());
    }

    const size_t capacity = buffer.capacity();
    EXPECT_GE(capacity, SocketBuffer::kLargeCapacityReleaseIfEmpty);

    while (buffer.size()) {
        EXPECT_EQ(buffer.capacity(), capacity);
        buffer.consume(1);
    }

    EXPECT_EQ(buffer.peek().second, 0);
    EXPECT_EQ(buffer.capacity(), 0);
}

TEST(SocketBuffer, snapshot) {
    DequeArchive archive;

    {
        SocketBuffer buffer;
        buffer.append("test", 4);
        buffer.saveToSnapshot(archive);
    }

    {
        SocketBuffer buffer;
        EXPECT_EQ(buffer.loadFromSnapshot(archive), 0);

        const auto [ptr, len] = buffer.peek();
        EXPECT_EQ(len, 4);
        EXPECT_EQ(::strncmp(static_cast<const char*>(ptr), "test", 4), 0);
    }
}
