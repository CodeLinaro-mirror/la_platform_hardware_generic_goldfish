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

// clang-format off
// IWYU pragma: begin_keep
#include "goldfish/archive/qemu_file_reader.h"
#include "goldfish/archive/qemu_file_writer.h"
#include "goldfish/qemu_file.h"
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::archive {

size_t QEMUFileReader::Read(void* dst, const size_t size) {
    return qemu_get_buffer(file, static_cast<uint8_t*>(dst), size);
}

void QEMUFileWriter::Write(const void* src, const size_t size) {
    qemu_put_buffer(file, static_cast<const uint8_t*>(src), size);
}

}  // namespace goldfish::archive
