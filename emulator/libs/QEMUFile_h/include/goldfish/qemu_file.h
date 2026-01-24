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

#if defined(__cplusplus) && defined(_WIN32)
extern "C" {
#endif

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/compiler.h"
#define coroutine_mixed_fn  // "qemu/osdep.h" breaks the Windows build
#include "migration/qemu-file-types.h"
// IWYU pragma: end_keep
// clang-format on

#if defined(__cplusplus) && defined(_WIN32)
}  // extern "C"
#endif
