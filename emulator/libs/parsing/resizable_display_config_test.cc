// Copyright (C) 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "goldfish/parsing/resizable_display_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace goldfish::parsing {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Optional;

using namespace std::string_view_literals;

TEST(ParseResizableDisplayConfig, empty) {
    EXPECT_THAT(ParseResizableDisplayConfig(""sv), Optional(IsEmpty()));
}

TEST(ParseResizableDisplayConfig, no_dpi) {
    const ResizableDisplayConfig cfg1 = {
        .name = "foo",
        .id = 42,
        .width = 640,
        .height = 480,
    };

    const ResizableDisplayConfig cfg2 = {
        .name = "bar",
        .id = 67,
        .width = 320,
        .height = 240,
    };

    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-640-480"sv), Optional(ElementsAre(cfg1)));
    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-640-480,bar-67-320-240"sv),
                Optional(ElementsAre(cfg1, cfg2)));
}

TEST(ParseResizableDisplayConfig, with_dpi) {
    const ResizableDisplayConfig cfg1 = {
        .name = "foo",
        .id = 42,
        .width = 640,
        .height = 480,
        .dpi = 96,
    };

    const ResizableDisplayConfig cfg2 = {
        .name = "bar",
        .id = 67,
        .width = 320,
        .height = 240,
        .dpi = 24,
    };

    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-640-480-96"sv), Optional(ElementsAre(cfg1)));
    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-640-480-96,bar-67-320-240-24"sv),
                Optional(ElementsAre(cfg1, cfg2)));
}

TEST(ParseResizableDisplayConfig, whitespace) {
    const ResizableDisplayConfig cfg1 = {
        .name = "foo",
        .id = 42,
        .width = 640,
        .height = 480,
        .dpi = 96,
    };

    const ResizableDisplayConfig cfg2 = {
        .name = "bar",
        .id = 67,
        .width = 320,
        .height = 240,
        .dpi = 24,
    };

    EXPECT_THAT(ParseResizableDisplayConfig(
                        "  foo -  42 - 640   -480  -96   , bar   - 67 - 320 - 240  - 24  "sv),
                Optional(ElementsAre(cfg1, cfg2)));
}

TEST(ParseResizableDisplayConfig, negative) {
    EXPECT_THAT(ParseResizableDisplayConfig(" "sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig("bad"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig(","sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig(",,"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig("bad,bad"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig(",bad,"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-640"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-640-480, -67-320-240-24"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig("foo-bad-640-480"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-bad-480"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-640-bad"sv), Eq(std::nullopt));
    EXPECT_THAT(ParseResizableDisplayConfig("foo-42-640-480-bad"sv), Eq(std::nullopt));
}

}  // namespace goldfish::parsing
