/* Copyright (C) 2026 The Android Open Source Project
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

#include <utility>

#include "absl/status/status.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

template <class T1, class T2>
absl::Status ReadValue(archive::IReader& r, std::pair<T1, T2>& x) {
    return ReadValue(r, x.first, x.second);
}

template <class T1, class T2>
IWriter& operator<<(IWriter& w, const std::pair<T1, T2>& x) {
    return w << x.first << x.second;
}

}  // namespace goldfish::archive
