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

#include "goldfish/sensors/foldable_model.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

namespace goldfish::sensors {

using namespace std::string_view_literals;

TEST(FoldableModelTest, ParseResizableConfigs_empty) {
    EXPECT_THAT(FoldableModel::ParseResizableConfigs(""sv), testing::Optional(testing::IsEmpty()));
}

TEST(FoldableModelTest, ParseResizableConfigs_no_dpi) {
    const FoldableModel::ResizableConfig cfg1 = {
        .name = "foo",
        .id = 42,
        .width = 640,
        .height = 480,
    };

    const FoldableModel::ResizableConfig cfg2 = {
        .name = "bar",
        .id = 67,
        .width = 320,
        .height = 240,
    };

    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-640-480"sv),
                testing::Optional(testing::ElementsAre(cfg1)));

    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-640-480,bar-67-320-240"sv),
                testing::Optional(testing::ElementsAre(cfg1, cfg2)));
}

TEST(FoldableModelTest, ParseResizableConfigs_with_dpi) {
    const FoldableModel::ResizableConfig cfg1 = {
        .name = "foo",
        .id = 42,
        .width = 640,
        .height = 480,
        .dpi = 96,
    };

    const FoldableModel::ResizableConfig cfg2 = {
        .name = "bar",
        .id = 67,
        .width = 320,
        .height = 240,
        .dpi = 24,
    };

    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-640-480-96"sv),
                testing::Optional(testing::ElementsAre(cfg1)));

    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-640-480-96,bar-67-320-240-24"sv),
                testing::Optional(testing::ElementsAre(cfg1, cfg2)));
}

TEST(FoldableModelTest, ParseResizableConfigs_whitespace) {
    const FoldableModel::ResizableConfig cfg1 = {
        .name = "foo",
        .id = 42,
        .width = 640,
        .height = 480,
        .dpi = 96,
    };

    const FoldableModel::ResizableConfig cfg2 = {
        .name = "bar",
        .id = 67,
        .width = 320,
        .height = 240,
        .dpi = 24,
    };

    EXPECT_THAT(FoldableModel::ParseResizableConfigs(
                        "  foo -  42 - 640   -480  -96   , bar   - 67 - 320 - 240  - 24  "sv),
                testing::Optional(testing::ElementsAre(cfg1, cfg2)));
}

TEST(FoldableModelTest, ParseResizableConfigs_bad) {
    EXPECT_THAT(FoldableModel::ParseResizableConfigs(" "sv), testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs("bad"sv), testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs(","sv), testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs(",,"sv), testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs("bad,bad"sv), testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs(",bad,"sv), testing::Eq(std::nullopt));

    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-640"sv), testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-640-480, -67-320-240-24"sv),
                testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-bad-640-480"sv),
                testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-bad-480"sv),
                testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-640-bad"sv),
                testing::Eq(std::nullopt));
    EXPECT_THAT(FoldableModel::ParseResizableConfigs("foo-42-640-480-bad"sv),
                testing::Eq(std::nullopt));
}

}  // namespace goldfish::sensors
