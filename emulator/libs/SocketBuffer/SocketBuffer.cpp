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
namespace {
size_t getCapacity(const size_t size) {
    return std::max(SocketBuffer::kMinCapacity, size * 3U / 2U);
}
}  // namespace

size_t SocketBuffer::append(const void* const appendData, const size_t appendSize) {
    assert(mSize <= mCapacity);

    const size_t newSize = mSize + appendSize;
    if (newSize > mCapacity) {
        const size_t newCapacity = getCapacity(newSize);
        assert(newCapacity >= newSize);
        std::unique_ptr<char[]> newData = std::make_unique<char[]>(newCapacity);

        if (mSize > 0) {
            assert(mConsume < mCapacity);
            assert(mData);

            if ((mConsume + mSize) <= mCapacity) {
                memcpy(&newData[0], &mData[mConsume], mSize);
            } else {
                const size_t sz = mCapacity - mConsume;
                memcpy(&newData[0], &mData[mConsume], sz);
                memcpy(&newData[sz], &mData[0], mSize - sz);
            }
        }

        memcpy(&newData[mSize], appendData, appendSize);

        mData = std::move(newData);
        mCapacity = newCapacity;
        mProduce = newSize;
        mConsume = 0;
    } else if (newSize == 0) {
        // do nothing
    } else if ((mProduce + appendSize) <= mCapacity) {
        assert(mCapacity > 0);
        assert(mProduce < mCapacity);
        assert(mData);

        memcpy(&mData[mProduce], appendData, appendSize);
        mProduce = (mProduce + appendSize) % mCapacity;
    } else {
        assert(mCapacity > 0);
        assert(mProduce < mCapacity);
        assert(mData);

        const char* appendData8 = static_cast<const char*>(appendData);
        const size_t sz1 = mCapacity - mProduce;
        assert(appendSize > sz1);
        const size_t sz2 = appendSize - sz1;

        memcpy(&mData[mProduce], appendData8, sz1);
        memcpy(&mData[0], appendData8 + sz1, sz2);
        mProduce = sz2;
    }

    mSize = newSize;
    return newSize;
}

std::pair<const void*, size_t> SocketBuffer::peek() const {
    assert(mSize <= mCapacity);
    if (mSize > 0) {
        assert(mConsume < mCapacity);
        assert(mData);

        return {&mData[mConsume], std::min(mSize, mCapacity - mConsume)};
    } else {
        return {nullptr, 0};
    }
}

size_t SocketBuffer::consume(const size_t size) {
    assert(mSize <= mCapacity);
    assert(size <= mSize);

    if (mCapacity) {
        if (mSize == size) {
            clear(mCapacity >= kLargeCapacityReleaseIfEmpty);
        } else {
            mSize -= size;
            mConsume = (mConsume + size) % mCapacity;
        }
    } else {
        assert(size == 0);
    }

    return mSize;
}

void SocketBuffer::clear(const bool alsoFreeMemory) {
    mSize = 0;
    mProduce = 0;
    mConsume = 0;

    if (alsoFreeMemory) {
        mData.reset();
        mCapacity = 0;
    }
}

void SocketBuffer::saveToSnapshot(archive::IWriter& writer) const {
    assert(mSize <= mCapacity);

    writer << mSize;
    if (mSize) {
        assert(mConsume < mCapacity);
        assert(mData);

        if ((mConsume + mSize) <= mCapacity) {
            writer.write(&mData[mConsume], mSize);
        } else {
            const size_t sz = mCapacity - mConsume;
            writer.write(&mData[mConsume], sz);
            writer.write(&mData[0], mSize - sz);
        }
    }
}

int SocketBuffer::loadFromSnapshot(archive::IReader& reader) {
    const size_t newSize = getUnsigned(reader);
    if (newSize == 0) {
        clear(true);
        return 0;
    }

    const size_t newCapacity = getCapacity(newSize);
    assert(newCapacity >= newSize);
    std::unique_ptr<char[]> newData = std::make_unique<char[]>(newCapacity);

    if (reader.read(newData.get(), newSize) != newSize) {
        return 1;
    }

    mData = std::move(newData);
    mCapacity = newCapacity;
    mSize = newSize;
    mProduce = newSize;
    mConsume = 0;
    return 0;
}

}  // namespace goldfish
