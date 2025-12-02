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

#include <gtest/gtest.h>
#include <pixman.h>

#include <thread>
#include <vector>

#include "goldfish/display/PixmanFrameManager.h"

using namespace goldfish::display;

class PixmanFrameManagerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mManager = std::make_unique<PixmanFrameManager>();
        mImage1 = PixmanImagePtr(
                pixman_image_create_bits(PIXMAN_a8r8g8b8, 100, 100, nullptr, 100 * 4));
        mImage2 = PixmanImagePtr(
                pixman_image_create_bits(PIXMAN_a8r8g8b8, 200, 200, nullptr, 200 * 4));
    }

    void TearDown() override {
        mImage1.reset();
        mImage2.reset();
    }

    std::unique_ptr<PixmanFrameManager> mManager;
    PixmanImagePtr mImage1;
    PixmanImagePtr mImage2;
};

TEST_F(PixmanFrameManagerTest, InitialImageIsNull) {
    auto image = mManager->getRenderableImage();
    EXPECT_EQ(image.get(), nullptr);
}

TEST_F(PixmanFrameManagerTest, UpdateAndGet) {
    mManager->updateSourceImage(mImage1.get());
    auto image = mManager->getRenderableImage();
    ASSERT_NE(image.get(), nullptr);
    EXPECT_EQ(pixman_image_get_width(image.get()), 100);
    EXPECT_EQ(pixman_image_get_height(image.get()), 100);
}

TEST_F(PixmanFrameManagerTest, Staging) {
    mManager->updateSourceImage(mImage1.get());
    // Staging image is not yet moved to current
    auto image = mManager->getRenderableImage();
    ASSERT_NE(image.get(), nullptr);
    EXPECT_EQ(pixman_image_get_width(image.get()), 100);
    EXPECT_EQ(pixman_image_get_height(image.get()), 100);

    mManager->updateSourceImage(mImage2.get());
    // Staging image is not yet moved to current
    image = mManager->getRenderableImage();
    ASSERT_NE(image.get(), nullptr);
    EXPECT_EQ(pixman_image_get_width(image.get()), 200);
    EXPECT_EQ(pixman_image_get_height(image.get()), 200);
}

TEST_F(PixmanFrameManagerTest, ConcurrentGet) {
    mManager->updateSourceImage(mImage1.get());

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this]() {
            auto image = mManager->getRenderableImage();
            ASSERT_NE(image.get(), nullptr);
            EXPECT_EQ(pixman_image_get_width(image.get()), 100);
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

TEST_F(PixmanFrameManagerTest, ConcurrentUpdateAndGet) {
    std::thread producer([this]() {
        for (int i = 0; i < 100; ++i) {
            mManager->updateSourceImage(i % 2 == 0 ? mImage1.get() : mImage2.get());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::vector<std::thread> consumers;
    for (int i = 0; i < 10; ++i) {
        consumers.emplace_back([this]() {
            for (int j = 0; j < 10; ++j) {
                auto image = mManager->getRenderableImage();
                if (image.get()) {
                    int width = pixman_image_get_width(image.get());
                    EXPECT_TRUE(width == 100 || width == 200);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    producer.join();
    for (auto& c : consumers) {
        c.join();
    }
}