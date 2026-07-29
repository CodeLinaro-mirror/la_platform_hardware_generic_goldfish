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

#include "absl/log/check.h"

namespace goldfish {
namespace {
size_t GetCapacity(const size_t size) {
    return std::max(SocketBuffer::kMinCapacity, size * 3U / 2U);
}
}  // namespace

size_t SocketBuffer::Append(const void* const append_data, const size_t append_size) {
    DCHECK(size_ <= capacity_);

    const size_t new_size = size_ + append_size;
    if (new_size > capacity_) {
        const size_t new_capacity = GetCapacity(new_size);
        DCHECK(new_capacity >= new_size);
        std::unique_ptr<char[]> new_data = std::make_unique<char[]>(new_capacity);

        if (size_ > 0) {
            DCHECK(consume_ < capacity_);
            DCHECK(data_);

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
        DCHECK(capacity_ > 0);
        DCHECK(produce_ < capacity_);
        DCHECK(data_);

        memcpy(&data_[produce_], append_data, append_size);
        produce_ = (produce_ + append_size) % capacity_;
    } else {
        DCHECK(capacity_ > 0);
        DCHECK(produce_ < capacity_);
        DCHECK(data_);

        const char* append_data8 = static_cast<const char*>(append_data);
        const size_t sz1 = capacity_ - produce_;
        DCHECK(append_size > sz1);
        const size_t sz2 = append_size - sz1;

        memcpy(&data_[produce_], append_data8, sz1);
        memcpy(&data_[0], append_data8 + sz1, sz2);
        produce_ = sz2;
    }

    size_ = new_size;
    return new_size;
}

std::pair<const void*, size_t> SocketBuffer::Peek() const {
    DCHECK(size_ <= capacity_);
    if (size_ > 0) {
        DCHECK(consume_ < capacity_);
        DCHECK(data_);

        return {&data_[consume_], std::min(size_, capacity_ - consume_)};
    }
    return {nullptr, 0};
}

size_t SocketBuffer::Consume(const size_t size) {
    DCHECK(size_ <= capacity_);
    DCHECK(size <= size_);

    if (capacity_) {
        if (size_ == size) {
            Clear(capacity_ >= kLargeCapacityReleaseIfEmpty);
        } else {
            size_ -= size;
            consume_ = (consume_ + size) % capacity_;
        }
    } else {
        DCHECK(size == 0);
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
    DCHECK(size_ <= capacity_);

    writer << size_;
    if (size_) {
        DCHECK(consume_ < capacity_);
        DCHECK(data_);

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
    uint32_t new_size = 0;
    if (!ReadValue(reader, new_size).ok()) {
        return 1;
    } else if (new_size == 0) {
        Clear(true);
        return 0;
    }

    const size_t new_capacity = GetCapacity(new_size);
    DCHECK(new_capacity >= new_size);
    std::unique_ptr<char[]> new_data = std::make_unique<char[]>(new_capacity);

    if (!reader.Read(new_data.get(), new_size).ok()) {
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
