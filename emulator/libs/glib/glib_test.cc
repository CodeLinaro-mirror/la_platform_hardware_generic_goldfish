/* Copyright (C) 2026 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <glib.h>
#include <gtest/gtest.h>

bool validate_name(const char* name) {
    if (!name || name[0] == '\0') {
        return false;
    }
    if (!g_ascii_isalnum(name[0]) || name[0] == '0') {
        return false;
    }
    return true;
}

TEST(GlibTest, AsciiIsAlnumAndNotZero) {
    EXPECT_TRUE(validate_name("a"));
    EXPECT_TRUE(validate_name("1"));
    EXPECT_FALSE(validate_name("0"));
    EXPECT_FALSE(validate_name(""));
    EXPECT_FALSE(validate_name("?"));
}
