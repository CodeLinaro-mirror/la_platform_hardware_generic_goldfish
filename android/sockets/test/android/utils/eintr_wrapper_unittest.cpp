// Copyright 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// Forces debug mode
#define EINTR_WRAPPER_DEBUG 1

#include "android/utils/eintr_wrapper.h"

#include <gtest/gtest.h>
#include <setjmp.h>
#include <stdarg.h>

// Loop counter used by several functions below.
static int loop_count = 0;

// This function returns the first time it is called, or -1/EINVAL
// otherwise.
static int return_einval_after_first_call(void) {
    if (++loop_count == 1) return 0;

    errno = EINVAL;
    return -1;
}

TEST(eintr_wrapper, NoLoopOnSuccess) {
    loop_count = 0;
    EXPECT_EQ(0, HANDLE_EINTR(return_einval_after_first_call()));
    EXPECT_EQ(1, loop_count);
}

TEST(eintr_wrapper, NoLoopOnRegularError) {
    loop_count = 0;
    EXPECT_EQ(0, HANDLE_EINTR(return_einval_after_first_call()));
    EXPECT_EQ(-1, HANDLE_EINTR(return_einval_after_first_call()));
    EXPECT_EQ(EINVAL, errno);
    EXPECT_EQ(2, loop_count);
}

static int always_return_eintr(void) {
    loop_count++;
#ifdef _WIN32
    // Win32 cannot generate EINTR.
    return 0;
#else
    errno = EINTR;
    return -1;
#endif
}

TEST(eintr_wrapper, IgnoreEintr) {
    loop_count = 0;
    EXPECT_EQ(0, IGNORE_EINTR(always_return_eintr()));
    EXPECT_EQ(1, loop_count);
}

#ifndef _WIN32

// This function loops 10 times around |loop_count|, while returning
// -1/errno.
static int loop_eintr_10(void) {
    if (++loop_count < 10) {
        errno = EINTR;
        return -1;
    }
    return 0;
}

TEST(eintr_wrapper, LoopOnEintr) {
    loop_count = 0;
    EXPECT_EQ(0, HANDLE_EINTR(loop_eintr_10()));
    EXPECT_EQ(10, loop_count);
}

#endif  // !_WIN32
