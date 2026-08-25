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

#include "goldfish/unique_id_allocator.h"

#include "absl/log/check.h"

#include "goldfish/archive/collections/set.h"

namespace goldfish {

uint32_t UniqueIdAllocator::Get() {
    auto i = returned_ids_.end();
    if (i != returned_ids_.begin()) {
        --i;
        const uint32_t id = *i;
        returned_ids_.erase(i);
        return id;
    }
    return ++last_id_;
}

void UniqueIdAllocator::Put(const uint32_t id) {
    if (id == last_id_) {
        --last_id_;

        while (true) {
            const auto i = returned_ids_.begin();
            if (i != returned_ids_.end() && *i == last_id_) {
                --last_id_;
                returned_ids_.erase(i);
            } else {
                break;
            }
        }
    } else {
        DCHECK(id < last_id_);
        returned_ids_.insert(id);
    }
}

void UniqueIdAllocator::Reset() {
    last_id_ = kEmptyId;
    returned_ids_.clear();
}

archive::IWriter& operator<<(archive::IWriter& writer, const UniqueIdAllocator& x) {
    return writer << x.last_id_ << x.returned_ids_;
}

absl::Status ReadValue(archive::IReader& reader, UniqueIdAllocator& x) {
    return ReadValue(reader, x.last_id_, x.returned_ids_);
}

}  // namespace goldfish
