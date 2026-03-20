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
#include "goldfish/parsing/hexbin.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>

namespace goldfish::parsing {
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Optional;

using namespace std::literals;

TEST(HexToBin, empty) {
    EXPECT_THAT(HexToBin(""sv), Optional(IsEmpty()));
}

TEST(HexToBin, positive) {
    EXPECT_THAT(HexToBin("01afc0"sv), Optional(ElementsAre(0x01, 0xAF, 0xC0)));
    EXPECT_THAT(HexToBin("01AFC0"sv), Optional(ElementsAre(0x01, 0xAF, 0xC0)));
    EXPECT_THAT(HexToBin("aB12fC"sv), Optional(ElementsAre(0xAB, 0x12, 0xFC)));
}

TEST(HexToBin, negative) {
    EXPECT_EQ(HexToBin("abc"sv), std::nullopt);
    EXPECT_EQ(HexToBin("01AG"sv), std::nullopt);
}

TEST(BinToHex, simple) {
    EXPECT_EQ(BinToHex({}), ""s);
    EXPECT_EQ(BinToHex(std::array<uint8_t, 1>{0x00}), "00"s);
    EXPECT_EQ(BinToHex(std::array<uint8_t, 2>{0x12, 0x34}), "1234"s);
    EXPECT_EQ(BinToHex(std::array<uint8_t, 3>{0xAB, 0xCD, 0xEF}), "abcdef"s);
}

}  // namespace goldfish::parsing
