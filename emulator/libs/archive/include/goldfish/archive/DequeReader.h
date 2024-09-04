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

#include "goldfish/archive/Reader.h"

namespace goldfish {
namespace archive {

/* This is mostly for tests, see archive_unittests.cpp */
struct DequeReader : public IReader {
    using Storage = std::deque<uint8_t>;

    explicit DequeReader(Storage* storage) : mStorage(storage) {}

    virtual size_t read(void* dst, size_t size) override;

    Storage* mStorage;
};

}  // namespace archive
}  // namespace goldfish
