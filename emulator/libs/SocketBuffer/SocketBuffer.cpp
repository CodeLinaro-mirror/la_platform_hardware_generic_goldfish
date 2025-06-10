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

#include "goldfish/SocketBuffer.h"

#include <cassert>

#include "goldfish/debug.h"

namespace goldfish {

void SocketBuffer::append(const void* data, size_t size) {
    if (mConsumed > 0) {
        mBuf.erase(mBuf.begin(), mBuf.begin() + mConsumed);
        mConsumed = 0;
    }

    const uint8_t* data8 = static_cast<const uint8_t*>(NOT_NULL(data));
    mBuf.insert(mBuf.end(), data8, data8 + size);
}

std::pair<const void*, size_t> SocketBuffer::peek() const {
    assert(mConsumed <= mBuf.size());
    return {mBuf.data() + mConsumed, mBuf.size() - mConsumed};
}

void SocketBuffer::consume(const size_t size) {
    assert((mConsumed + size) <= mBuf.size());
    mConsumed += size;
}

void SocketBuffer::saveToSnapshot(archive::IWriter& writer) const {
    const auto x = peek();
    writer << x.second;
    writer.write(x.first, x.second);
}

int SocketBuffer::loadFromSnapshot(archive::IReader& reader) {
    mConsumed = 0;
    const uint32_t size = getUnsigned(reader);
    mBuf.resize(size);
    return (reader.read(mBuf.data(), size) == size) ? 0 : 1;
}

}  // namespace goldfish
