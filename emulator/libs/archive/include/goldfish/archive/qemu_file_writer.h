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

#pragma once
#include "goldfish/archive/writer.h"

struct QEMUFile;

namespace goldfish {
namespace archive {

struct QEMUFileWriter : public IWriter {
    explicit QEMUFileWriter(QEMUFile* file) : mFile(file) {}

    virtual void write(const void* src, size_t size) override;

    QEMUFile* mFile;
};

}  // namespace archive
}  // namespace goldfish
