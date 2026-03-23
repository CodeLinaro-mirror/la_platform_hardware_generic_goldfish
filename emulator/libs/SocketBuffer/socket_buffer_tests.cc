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

#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/deque_reader.h"
#include "goldfish/archive/deque_writer.h"
#include "goldfish/socket_buffer.h"

using goldfish::SocketBuffer;
using goldfish::archive::DequeArchive;
using goldfish::archive::DequeReader;
using goldfish::archive::DequeWriter;

TEST(SocketBuffer, append_size) {
    SocketBuffer buffer;
    EXPECT_EQ(buffer.Append("test", 1), 1);
    EXPECT_EQ(buffer.Size(), 1);

    EXPECT_EQ(buffer.Append("test", 2), 1 + 2);
    EXPECT_EQ(buffer.Size(), 1 + 2);

    EXPECT_EQ(buffer.Append("test", 3), 1 + 2 + 3);
    EXPECT_EQ(buffer.Size(), 1 + 2 + 3);

    EXPECT_EQ(buffer.Append("test", 4), 1 + 2 + 3 + 4);
    EXPECT_EQ(buffer.Size(), 1 + 2 + 3 + 4);
}

TEST(SocketBuffer, append_empty_zero) {
    SocketBuffer buffer;
    EXPECT_EQ(buffer.Append("test", 0), 0);
    EXPECT_EQ(buffer.Append("test", 0), 0);
    EXPECT_EQ(buffer.Peek().second, 0);
    EXPECT_EQ(buffer.Capacity(), 0);
}

TEST(SocketBuffer, consume_empty_zero) {
    SocketBuffer buffer;
    (void)buffer.Consume(0);  // does not crash
    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_EQ(buffer.Peek().second, 0);
    EXPECT_EQ(buffer.Capacity(), 0);
}

TEST(SocketBuffer, clear) {
    SocketBuffer buffer;

    EXPECT_EQ(buffer.Append("test", 4), 4);
    const size_t capacity = buffer.Capacity();
    EXPECT_GE(capacity, buffer.Size());

    buffer.Clear(false);
    EXPECT_EQ(buffer.Capacity(), capacity);
    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_EQ(buffer.Peek().second, 0);

    buffer.Clear(true);
    EXPECT_EQ(buffer.Capacity(), 0);
    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_EQ(buffer.Peek().second, 0);
}

TEST(SocketBuffer, append_peek_consume) {
    SocketBuffer buffer;

    EXPECT_EQ(buffer.Append("Hello", 5), 5);
    EXPECT_EQ(buffer.Append(", world!", 8), 5 + 8);

    const size_t capacity = buffer.Capacity();
    EXPECT_GE(capacity, buffer.Size());
    EXPECT_LT(capacity, SocketBuffer::kLargeCapacityReleaseIfEmpty);  // see release_large_buffer

    {
        DequeArchive archive;
        buffer.SaveToSnapshot(archive);
        SocketBuffer loaded;
        EXPECT_EQ(loaded.LoadFromSnapshot(archive), 0);  // this will flatten it
        EXPECT_EQ(loaded.Size(), 13);

        const auto [ptr, len] = loaded.Peek();
        EXPECT_EQ(len, 13);
        EXPECT_EQ(::strncmp(static_cast<const char*>(ptr), "Hello, world!", 13), 0);
    }

    EXPECT_EQ(buffer.Consume(7), 5 + 8 - 7);

    {
        DequeArchive archive;
        buffer.SaveToSnapshot(archive);
        SocketBuffer loaded;
        EXPECT_EQ(loaded.LoadFromSnapshot(archive), 0);  // this will flatten it
        EXPECT_EQ(loaded.Size(), 6);

        const auto [ptr, len] = loaded.Peek();
        EXPECT_EQ(len, 6);
        EXPECT_EQ(::strncmp(static_cast<const char*>(ptr), "world!", 6), 0);
    }

    EXPECT_EQ(buffer.Consume(6), 5 + 8 - 7 - 6);

    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_EQ(buffer.Peek().second, 0);
    EXPECT_EQ(buffer.Capacity(), capacity);  // see release_large_buffer
}

TEST(SocketBuffer, release_large_buffer) {
    SocketBuffer buffer;

    {
        std::vector<char> data(SocketBuffer::kLargeCapacityReleaseIfEmpty);
        EXPECT_EQ(buffer.Append(data.data(), data.size()), data.size());
    }

    const size_t capacity = buffer.Capacity();
    EXPECT_GE(capacity, SocketBuffer::kLargeCapacityReleaseIfEmpty);

    while (buffer.Size()) {
        EXPECT_EQ(buffer.Capacity(), capacity);
        (void)buffer.Consume(1);
    }

    EXPECT_EQ(buffer.Peek().second, 0);
    EXPECT_EQ(buffer.Capacity(), 0);
}

TEST(SocketBuffer, snapshot) {
    DequeArchive archive;

    {
        SocketBuffer buffer;
        (void)buffer.Append("test", 4);
        buffer.SaveToSnapshot(archive);
    }

    {
        SocketBuffer buffer;
        EXPECT_EQ(buffer.LoadFromSnapshot(archive), 0);

        const auto [ptr, len] = buffer.Peek();
        EXPECT_EQ(len, 4);
        EXPECT_EQ(::strncmp(static_cast<const char*>(ptr), "test", 4), 0);
    }
}
