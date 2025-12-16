/* Copyright (C) 2024 The Android Open Source Project
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

#include "goldfish/archive/deque_archive.h"

#include <algorithm>

#include "goldfish/archive/deque_reader.h"
#include "goldfish/archive/deque_writer.h"
#include "goldfish/debug.h"

namespace goldfish::archive {

size_t DequeReader::Read(void* dst, const size_t requested_size) {
    const size_t size = std::min(requested_size, storage->size());
    const auto begin = storage->begin();
    const auto end = std::next(begin, static_cast<int64_t>(size));

    std::copy(begin, end, static_cast<uint8_t*>(NOT_NULL(dst)));
    NOT_NULL(storage)->erase(begin, end);

    return size;
}

void DequeWriter::Write(const void* src, const size_t size) {
    const auto* const src8 = static_cast<const uint8_t*>(NOT_NULL(src));
    NOT_NULL(storage)->insert(storage->end(), src8, src8 + size);
}

size_t DequeArchive::Read(void* dst, const size_t requested_size) {
    const size_t size = std::min(requested_size, storage.size());
    const auto begin = storage.cbegin();
    const auto end = std::next(begin, static_cast<int64_t>(size));

    std::copy(begin, end, static_cast<uint8_t*>(dst));
    storage.erase(begin, end);

    return size;
}

void DequeArchive::Write(const void* src, const size_t size) {
    const auto* const src8 = static_cast<const uint8_t*>(NOT_NULL(src));
    storage.insert(storage.end(), src8, src8 + size);
}

}  // namespace goldfish::archive
