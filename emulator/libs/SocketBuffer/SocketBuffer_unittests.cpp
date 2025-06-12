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

TEST(SocketBuffer, append_peek_consume) {
    SocketBuffer buffer;

    buffer.append("Hello", 5);
    buffer.append(", world!", 8);

    {
        const auto [ptr, len] = buffer.peek();
        EXPECT_EQ(len, 13);
        EXPECT_EQ(::strncmp(static_cast<const char*>(ptr), "Hello, world!", 13), 0);
    }

    buffer.consume(7);

    {
        const auto [ptr, len] = buffer.peek();
        EXPECT_EQ(len, 6);
        EXPECT_EQ(::strncmp(static_cast<const char*>(ptr), "world!", 6), 0);
    }

    buffer.consume(6);
    EXPECT_EQ(buffer.peek().second, 0);
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
