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

#include "goldfish/archive/Reader.h"
#include "goldfish/archive/Writer.h"

namespace goldfish {

struct SocketBuffer {
  constexpr static size_t kMinCapacity = 1024;
  constexpr static size_t kLargeCapacityReleaseIfEmpty = 4U << 20;

  size_t size() const { return mSize; }
  size_t capacity() const { return mCapacity; }

  size_t append(const void* data, size_t size);

  // Returns the contiguous portion of the buffer, it could be shorter than the whole buffer.
  std::pair<const void*, size_t> peek() const;

  // Consumes the `size` bytes from the buffer.
  // It must the less or equal than the value returned by `peek`.
  void consume(size_t size);

  void clear(bool alsoFreeMemory = false);

  void saveToSnapshot(archive::IWriter& writer) const;
  int loadFromSnapshot(archive::IReader& reader);

  SocketBuffer() = default;
  SocketBuffer(const SocketBuffer&) = delete;
  SocketBuffer(SocketBuffer&&) = delete;
  SocketBuffer& operator=(const SocketBuffer&) = delete;
  SocketBuffer& operator=(SocketBuffer&&) = delete;

 private:
  std::unique_ptr<char[]> mData;
  size_t mCapacity = 0;
  size_t mSize = 0;
  size_t mProduce = 0;
  size_t mConsume = 0;
};

}  // namespace goldfish
