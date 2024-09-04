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
#include <cstdint>
#include <deque>

#include "goldfish/archive/Writer.h"

namespace goldfish {
namespace archive {

/* This is mostly for tests, see archive_unittests.cpp */
struct DequeWriter : public IWriter {
    using Storage = std::deque<uint8_t>;

    explicit DequeWriter(Storage* storage) : mStorage(storage) {}

    virtual void write(const void* src, size_t size) override;

    Storage* mStorage;
};

}  // namespace archive
}  // namespace goldfish
