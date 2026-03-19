// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "goldfish/parsing/utf8_iterator.h"

#include <gtest/gtest.h>

using namespace std::string_view_literals;
using namespace goldfish::parsing;

TEST(Utf8IteratorTest, DefaultCtor) {
    Utf8Iterator it;
    EXPECT_EQ(-1, it());
    EXPECT_EQ(-1, it());
    EXPECT_EQ(-1, it());
}

TEST(Utf8IteratorTest, EmptySequence) {
    Utf8Iterator it("");
    EXPECT_EQ(-1, it());
}

TEST(Utf8IteratorTest, Constructors) {
    const uint8_t data[] = {'H', 'i'};
    Utf8Iterator it1(data, 2);
    EXPECT_EQ('H', it1());
    EXPECT_EQ('i', it1());
    EXPECT_EQ(-1, it1());

    Utf8Iterator it2(data, data + 2);
    EXPECT_EQ('H', it2());
    EXPECT_EQ('i', it2());
    EXPECT_EQ(-1, it2());
}

TEST(Utf8IteratorTest, ValidAscii) {
    Utf8Iterator it("Hello"sv);
    EXPECT_EQ('H', it());
    EXPECT_EQ('e', it());
    EXPECT_EQ('l', it());
    EXPECT_EQ('l', it());
    EXPECT_EQ('o', it());
    EXPECT_EQ(-1, it());
}

TEST(Utf8IteratorTest, ValidMultibyte) {
    // "¢" is U+00A2, encoded as C2 A2
    Utf8Iterator it("\xC2\xA2"sv);
    EXPECT_EQ(0x00A2, it());
    EXPECT_EQ(-1, it());

    // "ñ" is U+00F1, encoded as C3 B1
    Utf8Iterator it2("\xC3\xB1"sv);
    EXPECT_EQ(0x00F1, it2());
    EXPECT_EQ(-1, it2());
}

TEST(Utf8IteratorTest, ErrorCases) {
    // 0x80 is a continuation byte, invalid as leading
    Utf8Iterator it("\x80"sv);
    EXPECT_EQ(0xFFFD, it());
    EXPECT_EQ(-1, it());

    // 0xFF is never valid
    Utf8Iterator it2("\xFF"sv);
    EXPECT_EQ(0xFFFD, it2());
    EXPECT_EQ(-1, it2());

    // Missing continuation byte for 2-byte
    Utf8Iterator it3("\xC2"sv);
    EXPECT_EQ(0xFFFD, it3());
    EXPECT_EQ(-1, it3());

    // Missing continuation byte for 3-byte
    Utf8Iterator it4("\xE2\x82"sv);
    EXPECT_EQ(0xFFFD, it4());
    EXPECT_EQ(0xFFFD, it4());
    EXPECT_EQ(-1, it4());
}
