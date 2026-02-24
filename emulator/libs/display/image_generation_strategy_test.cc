// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the \"License\");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an \"AS IS\" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "emulator/libs/display/include/goldfish/display/test/image_generation_strategy.h"

#include <gtest/gtest.h>

#include "emulator/libs/display/include/goldfish/display/test/pixman_image_generator.h"
#include "goldfish/display/pixman_image_ptr.h"

using goldfish::display::PixmanImagePtr;
using goldfish::display::test::ChessboardStrategy;
using goldfish::display::test::Color;
using goldfish::display::test::FillColorStrategy;
using goldfish::display::test::LinearGradientStrategy;
using goldfish::display::test::PixmanImageGenerator;

// Test fixture for creating and managing a test pixman image.
class StrategyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_image_ = PixmanImagePtr(pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, kImageWidth,
                                                                       kImageHeight, nullptr, 0));
        ASSERT_NE(test_image_, nullptr);
    }

    void TearDown() override { test_image_.reset(); }

    PixmanImagePtr test_image_;
    static constexpr int kImageWidth = 512;
    static constexpr int kImageHeight = 512;
};

//------------------------------------------------------------------------------
// FillColorStrategy Tests
//------------------------------------------------------------------------------

TEST_F(StrategyTest, FillColorStrategyGeneratesCorrectColors) {
    FillColorStrategy strategy;

    // Frame 0 -> kRed
    strategy.Generate(test_image_.get(), 0);
    EXPECT_TRUE(strategy.IsGeneratedBy(test_image_.get(), 0));
    EXPECT_EQ(pixman_image_get_data(test_image_.get())[0], 0xffff0000);

    // Frame 1 -> kGreen
    strategy.Generate(test_image_.get(), 1);
    EXPECT_TRUE(strategy.IsGeneratedBy(test_image_.get(), 1));
    EXPECT_EQ(pixman_image_get_data(test_image_.get())[0], 0xff00ff00);

    // Frame 2 -> kBlue
    strategy.Generate(test_image_.get(), 2);
    EXPECT_TRUE(strategy.IsGeneratedBy(test_image_.get(), 2));
    EXPECT_EQ(pixman_image_get_data(test_image_.get())[0], 0xff0000ff);
}

TEST_F(StrategyTest, FillColorStrategyIsGeneratedByFailsForWrongFrame) {
    FillColorStrategy strategy;
    strategy.Generate(test_image_.get(), 0);                     // Generate kRed image
    EXPECT_FALSE(strategy.IsGeneratedBy(test_image_.get(), 1));  // Validate against kGreen
}

TEST_F(StrategyTest, FillColorStrategyIsGeneratedByFailsForWrongStrategy) {
    FillColorStrategy color_strategy;
    ChessboardStrategy chessboard_strategy;

    color_strategy.Generate(test_image_.get(), 0);  // Generate kRed image
    EXPECT_FALSE(chessboard_strategy.IsGeneratedBy(test_image_.get(), 0));
}

//------------------------------------------------------------------------------
// ChessboardStrategy Tests
//------------------------------------------------------------------------------

TEST_F(StrategyTest, ChessboardStrategyGeneratesCorrectPattern) {
    ChessboardStrategy strategy;
    strategy.Generate(test_image_.get(), 0);
    EXPECT_TRUE(strategy.IsGeneratedBy(test_image_.get(), 0));  // Frame number doesn't matter
    EXPECT_TRUE(strategy.IsGeneratedBy(test_image_.get(), 99));
}

TEST_F(StrategyTest, ChessboardStrategyIsGeneratedByFailsForWrongPattern) {
    ChessboardStrategy chessboard_strategy;
    FillColorStrategy color_strategy;
    color_strategy.Generate(test_image_.get(), 0);  // Generate solid color image
    EXPECT_FALSE(chessboard_strategy.IsGeneratedBy(test_image_.get(), 0));
}

//------------------------------------------------------------------------------
// LinearGradientStrategy Tests
//------------------------------------------------------------------------------

TEST_F(StrategyTest, LinearGradientStrategyGeneratesCorrectGradients) {
    LinearGradientStrategy strategy;

    // Frame 0 -> kRed Gradient
    strategy.Generate(test_image_.get(), 0);
    EXPECT_TRUE(strategy.IsGeneratedBy(test_image_.get(), 0));

    // Frame 1 -> kGreen Gradient
    strategy.Generate(test_image_.get(), 1);
    EXPECT_TRUE(strategy.IsGeneratedBy(test_image_.get(), 1));

    // Frame 2 -> kBlue Gradient
    strategy.Generate(test_image_.get(), 2);
    EXPECT_TRUE(strategy.IsGeneratedBy(test_image_.get(), 2));
}

TEST_F(StrategyTest, LinearGradientStrategyIsGeneratedByFailsForWrongGradient) {
    LinearGradientStrategy strategy;
    strategy.Generate(test_image_.get(), 0);                     // Generate red gradient
    EXPECT_FALSE(strategy.IsGeneratedBy(test_image_.get(), 1));  // Validate against green
}

//------------------------------------------------------------------------------
// PixmanImageGenerator Integration Tests
//------------------------------------------------------------------------------

TEST(PixmanImageGeneratorIntegrationTest, ConstructionAndGeneration) {
    constexpr int kImageWidth = 512;
    constexpr int kImageHeight = 512;
    PixmanImageGenerator generator(60, kImageWidth, kImageHeight);

    const PixmanImagePtr image = generator.GenerateImage(Color::kRed);
    ASSERT_TRUE(image);

    FillColorStrategy validator;
    EXPECT_TRUE(validator.IsGeneratedBy(image.get(), 0));  // 0 -> kRed
}

TEST(PixmanImageGeneratorIntegrationTest, GenerateAllColors) {
    constexpr int kImageWidth = 512;
    constexpr int kImageHeight = 512;
    PixmanImageGenerator generator(60, kImageWidth, kImageHeight);

    {
        // kRed
        const PixmanImagePtr image_red = generator.GenerateImage(Color::kRed);
        FillColorStrategy red_validator;
        EXPECT_TRUE(red_validator.IsGeneratedBy(image_red.get(), 0));
    }

    {
        // kGreen
        const PixmanImagePtr image_green = generator.GenerateImage(Color::kGreen);
        FillColorStrategy green_validator;
        EXPECT_TRUE(green_validator.IsGeneratedBy(image_green.get(), 1));
    }

    {
        // kBlue
        const PixmanImagePtr image_blue = generator.GenerateImage(Color::kBlue);
        FillColorStrategy blue_validator;
        EXPECT_TRUE(blue_validator.IsGeneratedBy(image_blue.get(), 2));
    }
}
