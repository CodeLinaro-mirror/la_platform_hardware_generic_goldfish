// Copyright 2026 The Android Open Source Project
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
#include "goldfish/gsm/alphabet.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace goldfish::gsm {
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Lt;

using namespace std::literals::string_view_literals;

TEST(IsInGsmDefaultAlphabet, positive) {
    for (uint32_t c = '0'; c <= '9'; ++c) {
        EXPECT_THAT(UnicodeToGsm7(c), AllOf(Ge(0), Lt(128)));
    }

    for (uint32_t c = 'A'; c <= 'Z'; ++c) {
        EXPECT_THAT(UnicodeToGsm7(c), AllOf(Ge(0), Lt(128)));
    }

    for (uint32_t c = 'a'; c <= 'z'; ++c) {
        EXPECT_THAT(UnicodeToGsm7(c), AllOf(Ge(0), Lt(128)));
    }

    EXPECT_EQ(UnicodeToGsm7('\n'), 10);
    EXPECT_EQ(UnicodeToGsm7('@'), 0);
    EXPECT_EQ(UnicodeToGsm7('_'), 17);
    EXPECT_EQ(UnicodeToGsm7(' '), 32);
    EXPECT_EQ(UnicodeToGsm7(0xA1), 64);

    EXPECT_EQ(UnicodeToGsm7('['), kGsm7ExtendedBit | 60);
    EXPECT_EQ(UnicodeToGsm7('~'), kGsm7ExtendedBit | 61);
    EXPECT_EQ(UnicodeToGsm7(0x20AC), kGsm7ExtendedBit | 101);
}

TEST(IsInGsmDefaultAlphabet, negative) {
    EXPECT_EQ(UnicodeToGsm7(0), -1);
    EXPECT_EQ(UnicodeToGsm7(0xFFFD), -1);
}

TEST(IsValidUcs2, positive) {
    EXPECT_TRUE(IsValidUcs2('0'));
    EXPECT_TRUE(IsValidUcs2('A'));
    EXPECT_TRUE(IsValidUcs2('a'));
    EXPECT_TRUE(IsValidUcs2('!'));
    EXPECT_TRUE(IsValidUcs2(0x100));
    EXPECT_TRUE(IsValidUcs2(0x1000));
    EXPECT_TRUE(IsValidUcs2(0x2000));
    EXPECT_TRUE(IsValidUcs2(0x4000));
    EXPECT_TRUE(IsValidUcs2(0x8000));
    EXPECT_TRUE(IsValidUcs2(0xFFFF));
}

TEST(IsValidUcs2, negative) {
    EXPECT_FALSE(IsValidUcs2(0x10000));
    EXPECT_FALSE(IsValidUcs2(0xD800));
    EXPECT_FALSE(IsValidUcs2(0xDFFF));
}

TEST(CalculateGsm7SizeSeptets, positive) {
    EXPECT_EQ(CalculateGsm7SizeSeptets(""sv), 0);
    EXPECT_EQ(CalculateGsm7SizeSeptets("cat"sv), 3);
    EXPECT_EQ(CalculateGsm7SizeSeptets("["sv), 2);
    EXPECT_EQ(CalculateGsm7SizeSeptets("[cat]"sv), 7);
    EXPECT_EQ(CalculateGsm7SizeSeptets("\xC3\xB1"sv), 1);
    EXPECT_EQ(CalculateGsm7SizeSeptets("\xE2\x82\xAC"sv), 2);
}

TEST(CalculateGsm7SizeSeptets, negative) {
    EXPECT_LT(CalculateGsm7SizeSeptets("\xF0\x9F\x9A\x80"sv), 0);
}

TEST(CalculateUcs2SizeSymbols, positive) {
    EXPECT_EQ(CalculateUcs2SizeSymbols(""sv), 0);
    EXPECT_EQ(CalculateUcs2SizeSymbols("cat"sv), 3);
    EXPECT_EQ(CalculateUcs2SizeSymbols("["sv), 1);
    EXPECT_EQ(CalculateUcs2SizeSymbols("[cat]"sv), 5);
    EXPECT_EQ(CalculateUcs2SizeSymbols("\xC3\xB1"), 1);
    EXPECT_EQ(CalculateUcs2SizeSymbols("\xE2\x82\xAC"sv), 1);
    EXPECT_EQ(CalculateUcs2SizeSymbols("cat\xC3\xB1"), 4);
}

TEST(CalculateUcs2SizeSymbols, negative) {
    EXPECT_LT(CalculateUcs2SizeSymbols("\xF0\x9F\x9A\x80"sv), 0);
}

}  // namespace goldfish::gsm
