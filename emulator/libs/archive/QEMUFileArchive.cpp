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

#include "goldfish/archive/QEMUFileReader.h"
#include "goldfish/archive/QEMUFileWriter.h"
#include "goldfish/QEMUFile.h"

namespace goldfish {
namespace archive {

size_t QEMUFileReader::read(void *dst, const size_t size) {
    return qemu_get_buffer(mFile, static_cast<uint8_t *>(dst), size);
}

void QEMUFileWriter::write(const void *src, const size_t size) {
    qemu_put_buffer(mFile, static_cast<const uint8_t *>(src), size);
}

}  // namespace archive
}  // namespace goldfish
