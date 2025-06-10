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
#include <vector>

#include "goldfish/archive/Reader.h"
#include "goldfish/archive/Writer.h"

namespace goldfish {

struct SocketBuffer {
  void append(const void* data, size_t size);
  std::pair<const void*, size_t> peek() const;
  void consume(size_t size);
  void saveToSnapshot(archive::IWriter& writer) const;
  int loadFromSnapshot(archive::IReader& reader);

 private:
  std::vector<uint8_t> mBuf;
  size_t mConsumed = 0;
};

}  // namespace goldfish
