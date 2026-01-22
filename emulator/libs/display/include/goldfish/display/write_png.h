/* Copyright (C) 2026 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#pragma once

#include <vector>

namespace goldfish::display {
// channel is 3 or 4
bool write_png(unsigned int nChannels, unsigned int width, unsigned int height, const void* pixels,
               std::vector<uint8_t>& out);
}  // namespace goldfish::display
