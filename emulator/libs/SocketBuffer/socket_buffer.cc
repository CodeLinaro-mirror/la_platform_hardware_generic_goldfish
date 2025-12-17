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

#include "goldfish/socket_buffer.h"

#include <cassert>

#include "goldfish/debug.h"

namespace goldfish {
namespace {
size_t GetCapacity(const size_t size) {
    return std::max(SocketBuffer::kMinCapacity, size * 3U / 2U);
}
}  // namespace

size_t SocketBuffer::Append(const void* const append_data, const size_t append_size) {
    assert(size_ <= capacity_);

    const size_t new_size = size_ + append_size;
    if (new_size > capacity_) {
        const size_t new_capacity = GetCapacity(new_size);
        assert(new_capacity >= new_size);
        std::unique_ptr<char[]> new_data = std::make_unique<char[]>(new_capacity);

        if (size_ > 0) {
            assert(consume_ < capacity_);
            assert(data_);

            if ((consume_ + size_) <= capacity_) {
                memcpy(&new_data[0], &data_[consume_], size_);
            } else {
                const size_t sz = capacity_ - consume_;
                memcpy(&new_data[0], &data_[consume_], sz);
                memcpy(&new_data[sz], &data_[0], size_ - sz);
            }
        }

        memcpy(&new_data[size_], append_data, append_size);

        data_ = std::move(new_data);
        capacity_ = new_capacity;
        produce_ = new_size;
        consume_ = 0;
    } else if (new_size == 0) {
        // do nothing
    } else if ((produce_ + append_size) <= capacity_) {
        assert(capacity_ > 0);
        assert(produce_ < capacity_);
        assert(data_);

        memcpy(&data_[produce_], append_data, append_size);
        produce_ = (produce_ + append_size) % capacity_;
    } else {
        assert(capacity_ > 0);
        assert(produce_ < capacity_);
        assert(data_);

        const char* append_data8 = static_cast<const char*>(append_data);
        const size_t sz1 = capacity_ - produce_;
        assert(append_size > sz1);
        const size_t sz2 = append_size - sz1;

        memcpy(&data_[produce_], append_data8, sz1);
        memcpy(&data_[0], append_data8 + sz1, sz2);
        produce_ = sz2;
    }

    size_ = new_size;
    return new_size;
}

std::pair<const void*, size_t> SocketBuffer::Peek() const {
    assert(size_ <= capacity_);
    if (size_ > 0) {
        assert(consume_ < capacity_);
        assert(data_);

        return {&data_[consume_], std::min(size_, capacity_ - consume_)};
    }
    return {nullptr, 0};
}

size_t SocketBuffer::Consume(const size_t size) {
    assert(size_ <= capacity_);
    assert(size <= size_);

    if (capacity_) {
        if (size_ == size) {
            Clear(capacity_ >= kLargeCapacityReleaseIfEmpty);
        } else {
            size_ -= size;
            consume_ = (consume_ + size) % capacity_;
        }
    } else {
        assert(size == 0);
    }

    return size_;
}

void SocketBuffer::Clear(const bool also_free_memory) {
    size_ = 0;
    produce_ = 0;
    consume_ = 0;

    if (also_free_memory) {
        data_.reset();
        capacity_ = 0;
    }
}

void SocketBuffer::SaveToSnapshot(archive::IWriter& writer) const {
    assert(size_ <= capacity_);

    writer << size_;
    if (size_) {
        assert(consume_ < capacity_);
        assert(data_);

        if ((consume_ + size_) <= capacity_) {
            writer.Write(&data_[consume_], size_);
        } else {
            const size_t sz = capacity_ - consume_;
            writer.Write(&data_[consume_], sz);
            writer.Write(&data_[0], size_ - sz);
        }
    }
}

int SocketBuffer::LoadFromSnapshot(archive::IReader& reader) {
    const size_t new_size = GetUnsigned(reader);
    if (new_size == 0) {
        Clear(true);
        return 0;
    }

    const size_t new_capacity = GetCapacity(new_size);
    assert(new_capacity >= new_size);
    std::unique_ptr<char[]> new_data = std::make_unique<char[]>(new_capacity);

    if (reader.Read(new_data.get(), new_size) != new_size) {
        return 1;
    }

    data_ = std::move(new_data);
    capacity_ = new_capacity;
    size_ = new_size;
    produce_ = new_size;
    consume_ = 0;
    return 0;
}

}  // namespace goldfish
