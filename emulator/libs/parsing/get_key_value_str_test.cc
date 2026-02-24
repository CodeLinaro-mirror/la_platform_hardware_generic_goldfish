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

#include "goldfish/parsing/get_key_value_str.h"

#include <gtest/gtest.h>

using namespace std::string_view_literals;
using namespace goldfish::parsing;

TEST(GetKeyValueStr, positive) {
    EXPECT_EQ(GetKeyValueStr("empty="sv, "empty"sv), ""sv);
    EXPECT_EQ(GetKeyValueStr(" empty="sv, "empty"sv), ""sv);
    EXPECT_EQ(GetKeyValueStr("empty= "sv, "empty"sv), ""sv);
    EXPECT_EQ(GetKeyValueStr(" empty= "sv, "empty"sv), ""sv);

    EXPECT_EQ(GetKeyValueStr("one=v1"sv, "one"sv), "v1"sv);
    EXPECT_EQ(GetKeyValueStr(" one=v1"sv, "one"sv), "v1"sv);
    EXPECT_EQ(GetKeyValueStr("one=v1 "sv, "one"sv), "v1"sv);
    EXPECT_EQ(GetKeyValueStr(" one=v1 "sv, "one"sv), "v1"sv);

    constexpr std::string_view text = "one=v1 two=v2 three=v3 one=dup"sv;

    EXPECT_EQ(GetKeyValueStr(text, "one"sv), "v1"sv);
    EXPECT_EQ(GetKeyValueStr(text, "two"sv), "v2"sv);
    EXPECT_EQ(GetKeyValueStr(text, "three"sv), "v3"sv);
}

TEST(GetKeyValueStr, negative) {
    EXPECT_EQ(GetKeyValueStr(""sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr(" "sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("  "sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("kawabunga"sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr(" kawabunga"sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("kawabunga "sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr(" kawabunga "sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("key"sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("keykey=foo"sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("keykey=foo "sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr(" keykey=foo"sv, "key"sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr(" keykey=foo "sv, "key"sv), std::nullopt);
}

TEST(GetKeyValueStr, emptyKey) {
    EXPECT_EQ(GetKeyValueStr(""sv, ""sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr(" "sv, ""sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("  "sv, ""sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("kawabunga"sv, ""sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr(" kawabunga"sv, ""sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr("kawabunga "sv, ""sv), std::nullopt);
    EXPECT_EQ(GetKeyValueStr(" kawabunga "sv, ""sv), std::nullopt);
}
