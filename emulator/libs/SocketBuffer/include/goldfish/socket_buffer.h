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

#pragma once

#include <cstdint>
#include <memory>

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish {

struct SocketBuffer {
    constexpr static size_t kMinCapacity = 1024;
    constexpr static size_t kLargeCapacityReleaseIfEmpty = 4U << 20;

    size_t Size() const { return size_; }
    size_t Capacity() const { return capacity_; }

    /**
     * Appends the data to the buffer.
     *
     * You want to check the returned size (and potentially pause the
     * producer) to prevent unbounded growth of the buffer.
     */
    [[nodiscard]] size_t Append(const void* data, size_t size);

    // Returns the contiguous portion of the buffer, it could be shorter than the whole buffer.
    std::pair<const void*, size_t> Peek() const;

    /**
     * Consumes the `size` bytes from the buffer.
     * It must the less or equal than the value returned by `peek`.
     *
     * You want to check the returned size to resume the producer.
     */
    [[nodiscard]] size_t Consume(size_t size);

    void Clear(bool also_free_memory = false);

    void SaveToSnapshot(archive::IWriter& writer) const;
    int LoadFromSnapshot(archive::IReader& reader);

    SocketBuffer() = default;
    SocketBuffer(const SocketBuffer&) = delete;
    SocketBuffer(SocketBuffer&&) = delete;
    SocketBuffer& operator=(const SocketBuffer&) = delete;
    SocketBuffer& operator=(SocketBuffer&&) = delete;

  private:
    std::unique_ptr<char[]> data_;
    size_t capacity_ = 0;
    size_t size_ = 0;
    size_t produce_ = 0;
    size_t consume_ = 0;
};

}  // namespace goldfish
