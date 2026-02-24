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

#include "goldfish/parsing/split2.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace std::string_view_literals;
using namespace goldfish::parsing;
using ::testing::Pair;

TEST(spli2, simple) {
    EXPECT_THAT(Split2(""sv, ' '), Pair(""sv, ""sv));
    EXPECT_THAT(Split2("one"sv, ' '), Pair("one"sv, ""sv));
    EXPECT_THAT(Split2("one "sv, ' '), Pair("one"sv, ""sv));
    EXPECT_THAT(Split2("one  "sv, ' '), Pair("one"sv, " "sv));
    EXPECT_THAT(Split2("one  two"sv, ' '), Pair("one"sv, " two"sv));
    EXPECT_THAT(Split2("one  two"sv, ' '), Pair("one"sv, " two"sv));
}
