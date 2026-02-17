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

#include "goldfish/display/write_png.h"

#include "png.h"

namespace goldfish::display {
namespace {
bool WritePngUserFunction(png_structp p, png_infop pi, unsigned int n_channels, unsigned int width,
                          unsigned int height, const void* pixels) {
    if (n_channels != 3 && n_channels != 4) {
        return false;
    }
    if (!pixels) {
        return false;
    }

    const unsigned int rows = height;
    const unsigned int cols = width;

    if (setjmp(png_jmpbuf(p))) {
        return false;
    }

    const int z_best_speed = 1;
    png_set_compression_level(p, z_best_speed);

    png_set_IHDR(p, pi, cols, rows, 8,
                 n_channels == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGB_ALPHA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, pi);

    if (setjmp(png_jmpbuf(p))) {
        return false;
    }
    const auto* upixels = reinterpret_cast<const uint8_t*>(pixels);
    {
        unsigned int i = 0;
        for (i = 0; i < height; i++) {
            png_write_row(p, upixels + (static_cast<size_t>(i) * n_channels * width));
        }
    }
    if (setjmp(png_jmpbuf(p))) {
        return false;
    }
    png_write_end(p, nullptr);
    return true;
}
}  // namespace

bool write_png(unsigned int n_channels, unsigned int width, unsigned int height, const void* pixels,
               std::vector<uint8_t>& png_data) {
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop pi = png_create_info_struct(p);
    png_set_write_fn(
            p, &png_data,
            [](png_structp png_ptr, png_bytep data, png_size_t length) {
                auto* vec = reinterpret_cast<std::vector<uint8_t>*>(png_get_io_ptr(png_ptr));
                vec->insert(vec->end(), &data[0], &data[length]);
            },
            [](png_structp png_ptr) {});
    const bool result = WritePngUserFunction(p, pi, n_channels, width, height, pixels);
    png_destroy_write_struct(&p, &pi);
    return result;
}
}  // namespace goldfish::display
