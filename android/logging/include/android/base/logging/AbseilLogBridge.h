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
#include <assert.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

// Make sure C++ hackers do not use this..
#ifdef __cplusplus
#define CPLUSPLUS_WARNING \
    [[deprecated("Please use the absl logging library instead of ALOG... macros for C++.")]]
#else
#define CPLUSPLUS_WARNING
#endif

CPLUSPLUS_WARNING extern void _log_to_abseil(int severity, const char* file, unsigned int line,
                                             const char* format, ...);

CPLUSPLUS_WARNING extern void _vlog_to_abseil(void* vlog_site, int severity, unsigned int line,
                                              const char* format, ...);

// Should return a VLogSite* info object
extern void* _get_vlog_site(const char* name);

// Lazily initializes a VLogSite info for the file that includes this
// Note: you cannot use VLOG macros in both header and .c files, since
// unlike C++ we cannot declare a local lambda function with a static
// initializer.
static void* _vlog_site(const char* file) {
    static void* vlog = _get_vlog_site(file);
#ifndef NDEBUG
    static const char* first = file;
    assert(first == file);  // You can only use the VLOG macro in a single file.
#endif
    return vlog;
}

#define ALOGI(FMT, ...) _log_to_abseil(0, __FILE__, __LINE__, FMT, ##__VA_ARGS__)
#define ALOGW(FMT, ...) _log_to_abseil(1, __FILE__, __LINE__, FMT, ##__VA_ARGS__)
#define ALOGE(FMT, ...) _log_to_abseil(2, __FILE__, __LINE__, FMT, ##__VA_ARGS__)
#define ALOGF(FMT, ...) _log_to_abseil(3, __FILE__, __LINE__, FMT, ##__VA_ARGS__)

// Note that level > 0
// You should not use this macro in header files.
#define ALOGV(LEVEL, FMT, ...)                                 \
    _Static_assert(LEVEL > 0, "LEVEL must be greater than 0"); \
    _vlog_to_abseil(_vlog_site(__FILE__), LEVEL, __LINE__, FMT, ##__VA_ARGS__)

__END_DECLS
