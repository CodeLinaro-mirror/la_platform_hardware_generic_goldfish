// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#pragma once
#include <sys/cdefs.h>

__BEGIN_DECLS

extern void _log_to_abseil(int severity, const char* file, unsigned int line, const char* format,
                           ...);

#define ALOGI(FMT, ...) _log_to_abseil(0, __FILE__, __LINE__, FMT, ##__VA_ARGS__)
#define ALOGW(FMT, ...) _log_to_abseil(1, __FILE__, __LINE__, FMT, ##__VA_ARGS__)
#define ALOGE(FMT, ...) _log_to_abseil(2, __FILE__, __LINE__, FMT, ##__VA_ARGS__)
#define ALOGF(FMT, ...) _log_to_abseil(3, __FILE__, __LINE__, FMT, ##__VA_ARGS__)

// Note that level > 0
#define ALOGV(LEVEL, FMT, ...)                                 \
    _Static_assert(LEVEL > 0, "LEVEL must be greater than 0"); \
    _log_to_abseil(-(LEVEL), __FILE__, __LINE__, FMT, ##__VA_ARGS__)

__END_DECLS
