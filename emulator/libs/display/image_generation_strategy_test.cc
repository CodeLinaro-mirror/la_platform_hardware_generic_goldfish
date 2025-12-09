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

#include "emulator/libs/display/include/goldfish/display/test/image_generation_strategy.h"

#include <gtest/gtest.h>

#include "emulator/libs/display/include/goldfish/display/test/pixman_image_generator.h"
#include "goldfish/display/pixman_image_ptr.h"

using goldfish::display::PixmanImagePtr;
using namespace goldfish::display::test;

// Test fixture for creating and managing a test pixman image.
class StrategyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mTestImage = PixmanImagePtr(pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8, kImageWidth,
                                                                      kImageHeight, nullptr, 0));
        ASSERT_NE(mTestImage, nullptr);
    }

    void TearDown() override { mTestImage.reset(); }

    PixmanImagePtr mTestImage;
    static constexpr int kImageWidth = 512;
    static constexpr int kImageHeight = 512;
};

//------------------------------------------------------------------------------
// FillColorStrategy Tests
//------------------------------------------------------------------------------

TEST_F(StrategyTest, FillColorStrategy_GeneratesCorrectColors) {
    FillColorStrategy strategy;

    // Frame 0 -> Red
    strategy.generate(mTestImage.get(), 0);
    EXPECT_TRUE(strategy.isGeneratedBy(mTestImage.get(), 0));
    EXPECT_EQ(pixman_image_get_data(mTestImage.get())[0], 0xffff0000);

    // Frame 1 -> Green
    strategy.generate(mTestImage.get(), 1);
    EXPECT_TRUE(strategy.isGeneratedBy(mTestImage.get(), 1));
    EXPECT_EQ(pixman_image_get_data(mTestImage.get())[0], 0xff00ff00);

    // Frame 2 -> Blue
    strategy.generate(mTestImage.get(), 2);
    EXPECT_TRUE(strategy.isGeneratedBy(mTestImage.get(), 2));
    EXPECT_EQ(pixman_image_get_data(mTestImage.get())[0], 0xff0000ff);
}

TEST_F(StrategyTest, FillColorStrategy_IsGeneratedByFailsForWrongFrame) {
    FillColorStrategy strategy;
    strategy.generate(mTestImage.get(), 0);                     // Generate Red image
    EXPECT_FALSE(strategy.isGeneratedBy(mTestImage.get(), 1));  // Validate against Green
}

TEST_F(StrategyTest, FillColorStrategy_IsGeneratedByFailsForWrongStrategy) {
    FillColorStrategy colorStrategy;
    ChessboardStrategy chessboardStrategy;

    colorStrategy.generate(mTestImage.get(), 0);  // Generate Red image
    EXPECT_FALSE(chessboardStrategy.isGeneratedBy(mTestImage.get(), 0));
}

//------------------------------------------------------------------------------
// ChessboardStrategy Tests
//------------------------------------------------------------------------------

TEST_F(StrategyTest, ChessboardStrategy_GeneratesCorrectPattern) {
    ChessboardStrategy strategy;
    strategy.generate(mTestImage.get(), 0);
    EXPECT_TRUE(strategy.isGeneratedBy(mTestImage.get(), 0));  // Frame number doesn't matter
    EXPECT_TRUE(strategy.isGeneratedBy(mTestImage.get(), 99));
}

TEST_F(StrategyTest, ChessboardStrategy_IsGeneratedByFailsForWrongPattern) {
    ChessboardStrategy chessboardStrategy;
    FillColorStrategy colorStrategy;
    colorStrategy.generate(mTestImage.get(), 0);  // Generate solid color image
    EXPECT_FALSE(chessboardStrategy.isGeneratedBy(mTestImage.get(), 0));
}

//------------------------------------------------------------------------------
// LinearGradientStrategy Tests
//------------------------------------------------------------------------------

TEST_F(StrategyTest, LinearGradientStrategy_GeneratesCorrectGradients) {
    LinearGradientStrategy strategy;

    // Frame 0 -> Red Gradient
    strategy.generate(mTestImage.get(), 0);
    EXPECT_TRUE(strategy.isGeneratedBy(mTestImage.get(), 0));

    // Frame 1 -> Green Gradient
    strategy.generate(mTestImage.get(), 1);
    EXPECT_TRUE(strategy.isGeneratedBy(mTestImage.get(), 1));

    // Frame 2 -> Blue Gradient
    strategy.generate(mTestImage.get(), 2);
    EXPECT_TRUE(strategy.isGeneratedBy(mTestImage.get(), 2));
}

TEST_F(StrategyTest, LinearGradientStrategy_IsGeneratedByFailsForWrongGradient) {
    LinearGradientStrategy strategy;
    strategy.generate(mTestImage.get(), 0);                     // Generate red gradient
    EXPECT_FALSE(strategy.isGeneratedBy(mTestImage.get(), 1));  // Validate against green
}

//------------------------------------------------------------------------------
// PixmanImageGenerator Integration Tests
//------------------------------------------------------------------------------

TEST(PixmanImageGeneratorIntegrationTest, ConstructionAndGeneration) {
    constexpr int kImageWidth = 512;
    constexpr int kImageHeight = 512;
    PixmanImageGenerator generator(60, kImageWidth, kImageHeight);

    PixmanImagePtr image = generator.generateImage(Color::Red);
    ASSERT_TRUE(image);

    FillColorStrategy validator;
    EXPECT_TRUE(validator.isGeneratedBy(image.get(), 0));  // 0 -> Red
}

TEST(PixmanImageGeneratorIntegrationTest, GenerateAllColors) {
    constexpr int kImageWidth = 512;
    constexpr int kImageHeight = 512;
    PixmanImageGenerator generator(60, kImageWidth, kImageHeight);

    {
        // Red
        PixmanImagePtr image_red = generator.generateImage(Color::Red);
        FillColorStrategy red_validator;
        EXPECT_TRUE(red_validator.isGeneratedBy(image_red.get(), 0));
    }

    {
        // Green
        PixmanImagePtr image_green = generator.generateImage(Color::Green);
        FillColorStrategy green_validator;
        EXPECT_TRUE(green_validator.isGeneratedBy(image_green.get(), 1));
    }

    {
        // Blue
        PixmanImagePtr image_blue = generator.generateImage(Color::Blue);
        FillColorStrategy blue_validator;
        EXPECT_TRUE(blue_validator.isGeneratedBy(image_blue.get(), 2));
    }
}
