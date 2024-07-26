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

#include <algorithm>

#include "goldfish/archive/DequeArchive.h"
#include "goldfish/archive/DequeReader.h"
#include "goldfish/archive/DequeWriter.h"
#include "goldfish/debug.h"

namespace goldfish {
namespace archive {

size_t DequeReader::read(void *dst, const size_t requestedSize) {
    const size_t size = std::min(requestedSize, mStorage->size());
    const auto begin = mStorage->begin();
    const auto end = begin + size;

    std::copy(begin, end, static_cast<uint8_t *>(NOT_NULL(dst)));
    NOT_NULL(mStorage)->erase(begin, end);

    return size;
}

void DequeWriter::write(const void *src, const size_t size) {
    const auto src8 = static_cast<const uint8_t *>(NOT_NULL(src));
    NOT_NULL(mStorage)->insert(mStorage->end(), src8, src8 + size);
}

size_t DequeArchive::read(void *dst, const size_t requestedSize) {
    const size_t size = std::min(requestedSize, storage.size());
    const auto begin = storage.begin();
    const auto end = begin + size;

    std::copy(begin, end, static_cast<uint8_t *>(dst));
    storage.erase(begin, end);

    return size;
}

void DequeArchive::write(const void *src, const size_t size) {
    const auto src8 = static_cast<const uint8_t *>(NOT_NULL(src));
    storage.insert(storage.end(), src8, src8 + size);
}

}  // namespace archive
}  // namespace goldfish
