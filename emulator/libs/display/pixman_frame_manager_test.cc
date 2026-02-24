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

#include "goldfish/display/pixman_frame_manager.h"

#include <gtest/gtest.h>
#include <pixman.h>

#include <thread>
#include <vector>

using goldfish::display::PixmanFrameManager;
using goldfish::display::PixmanImagePtr;

class PixmanFrameManagerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        manager_ = std::make_unique<PixmanFrameManager>();
        image1_ = PixmanImagePtr(
                pixman_image_create_bits(PIXMAN_a8r8g8b8, 100, 100, nullptr, 100 * 4));
        image2_ = PixmanImagePtr(
                pixman_image_create_bits(PIXMAN_a8r8g8b8, 200, 200, nullptr, 200 * 4));
    }

    void TearDown() override {
        image1_.reset();
        image2_.reset();
    }

    std::unique_ptr<PixmanFrameManager> manager_;
    PixmanImagePtr image1_;
    PixmanImagePtr image2_;
};

TEST_F(PixmanFrameManagerTest, InitialImageIsNull) {
    auto image = manager_->GetRenderableImage();
    EXPECT_EQ(image.get(), nullptr);
}

TEST_F(PixmanFrameManagerTest, UpdateAndGet) {
    manager_->UpdateSourceImage(image1_.get());
    auto image = manager_->GetRenderableImage();
    ASSERT_NE(image.get(), nullptr);
    EXPECT_EQ(pixman_image_get_width(image.get()), 100);
    EXPECT_EQ(pixman_image_get_height(image.get()), 100);
}

TEST_F(PixmanFrameManagerTest, Staging) {
    manager_->UpdateSourceImage(image1_.get());
    // Staging image is not yet moved to current
    auto image = manager_->GetRenderableImage();
    ASSERT_NE(image.get(), nullptr);
    EXPECT_EQ(pixman_image_get_width(image.get()), 100);
    EXPECT_EQ(pixman_image_get_height(image.get()), 100);

    manager_->UpdateSourceImage(image2_.get());
    // Staging image is not yet moved to current
    image = manager_->GetRenderableImage();
    ASSERT_NE(image.get(), nullptr);
    EXPECT_EQ(pixman_image_get_width(image.get()), 200);
    EXPECT_EQ(pixman_image_get_height(image.get()), 200);
}

TEST_F(PixmanFrameManagerTest, ConcurrentGet) {
    manager_->UpdateSourceImage(image1_.get());

    std::vector<std::thread> threads;
    threads.reserve(10);
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this]() {
            auto image = manager_->GetRenderableImage();
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
            manager_->UpdateSourceImage(i % 2 == 0 ? image1_.get() : image2_.get());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::vector<std::thread> consumers;
    consumers.reserve(10);
    for (int i = 0; i < 10; ++i) {
        consumers.emplace_back([this]() {
            for (int j = 0; j < 10; ++j) {
                auto image = manager_->GetRenderableImage();
                if (image.get()) {
                    const int width = pixman_image_get_width(image.get());
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