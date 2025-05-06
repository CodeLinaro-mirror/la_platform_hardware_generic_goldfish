// Copyright (C) 2025 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "goldfish/parsing/getKeyValueStr.h"

#include <gtest/gtest.h>

using namespace std::string_view_literals;
using namespace goldfish::parsing;

TEST(getKeyValueStr, positive) {
    EXPECT_EQ(getKeyValueStr("empty="sv, "empty"sv), ""sv);
    EXPECT_EQ(getKeyValueStr(" empty="sv, "empty"sv), ""sv);
    EXPECT_EQ(getKeyValueStr("empty= "sv, "empty"sv), ""sv);
    EXPECT_EQ(getKeyValueStr(" empty= "sv, "empty"sv), ""sv);

    EXPECT_EQ(getKeyValueStr("one=v1"sv, "one"sv), "v1"sv);
    EXPECT_EQ(getKeyValueStr(" one=v1"sv, "one"sv), "v1"sv);
    EXPECT_EQ(getKeyValueStr("one=v1 "sv, "one"sv), "v1"sv);
    EXPECT_EQ(getKeyValueStr(" one=v1 "sv, "one"sv), "v1"sv);

    constexpr std::string_view text = "one=v1 two=v2 three=v3 one=dup"sv;

    EXPECT_EQ(getKeyValueStr(text, "one"sv), "v1"sv);
    EXPECT_EQ(getKeyValueStr(text, "two"sv), "v2"sv);
    EXPECT_EQ(getKeyValueStr(text, "three"sv), "v3"sv);
}

TEST(getKeyValueStr, negative) {
    EXPECT_EQ(getKeyValueStr(""sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr(" "sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("  "sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("kawabunga"sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr(" kawabunga"sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("kawabunga "sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr(" kawabunga "sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("key"sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("keykey=foo"sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("keykey=foo "sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr(" keykey=foo"sv, "key"sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr(" keykey=foo "sv, "key"sv), std::nullopt);
}

TEST(getKeyValueStr, emptyKey) {
    EXPECT_EQ(getKeyValueStr(""sv, ""sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr(" "sv, ""sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("  "sv, ""sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("kawabunga"sv, ""sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr(" kawabunga"sv, ""sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr("kawabunga "sv, ""sv), std::nullopt);
    EXPECT_EQ(getKeyValueStr(" kawabunga "sv, ""sv), std::nullopt);
}
