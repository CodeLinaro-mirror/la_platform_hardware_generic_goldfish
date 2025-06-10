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

#include <set>

#include "goldfish/archive/Reader.h"
#include "goldfish/archive/Writer.h"

namespace goldfish {

/*
 * A class that allocates (via `get`) unique ids. The returned (via `put`) ids
 * are recycled and can be used later. Returning an id that was not allocated
 * before is undefined behavior.
 *
 * `UniqueIdAllocator::kEmptyId` represents an empty id.
 *
 * This class can be saved to a snapshot to be restored later.
 */
struct UniqueIdAllocator {
  static constexpr uint32_t kEmptyId = 0;

  uint32_t get();
  void put(uint32_t id);
  void reset();
  void saveToSnapshot(archive::IWriter& writer) const;
  int loadFromSnapshot(archive::IReader& reader);

 private:
  uint32_t mLastId = kEmptyId;
  std::set<uint32_t, std::greater<uint32_t>> mReturnedIds;
};

}  // namespace goldfish
