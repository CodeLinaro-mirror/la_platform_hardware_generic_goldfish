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

#include <cassert>

namespace goldfish {

uint32_t UniqueIdAllocator::get() {
    auto i = mReturnedIds.end();
    if (i != mReturnedIds.begin()) {
        --i;
        const uint32_t id = *i;
        mReturnedIds.erase(i);
        return id;
    } else {
        return ++mLastId;
    }
}

void UniqueIdAllocator::put(const uint32_t id) {
    if (id == mLastId) {
        --mLastId;

        while (true) {
            const auto i = mReturnedIds.begin();
            if (i != mReturnedIds.end() && *i == mLastId) {
                --mLastId;
                mReturnedIds.erase(i);
            } else {
                break;
            }
        }
    } else {
        assert(id < mLastId);
        mReturnedIds.insert(id);
    }
}

void UniqueIdAllocator::reset() {
    mLastId = kEmptyId;
    mReturnedIds.clear();
}

void UniqueIdAllocator::saveToSnapshot(archive::IWriter& writer) const {
    writer << mLastId << mReturnedIds.size();
    for (const uint32_t id : mReturnedIds) {
        writer << id;
    }
}

int UniqueIdAllocator::loadFromSnapshot(archive::IReader& reader) {
    mLastId = getUnsigned(reader);
    mReturnedIds.clear();
    for (size_t n = getUnsigned(reader); n > 0; --n) {
        mReturnedIds.insert(getUnsigned(reader));
    }

    return 0;
}

}  // namespace goldfish
