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

#include "absl/status/status.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

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

    uint32_t Get();
    void Put(uint32_t id);
    void Reset();

    friend archive::IWriter& operator<<(archive::IWriter& writer, const UniqueIdAllocator&);
    friend absl::Status ReadValue(archive::IReader& reader, UniqueIdAllocator&);

  private:
    uint32_t last_id_ = kEmptyId;
    std::set<uint32_t, std::greater<>> returned_ids_;
};

}  // namespace goldfish
