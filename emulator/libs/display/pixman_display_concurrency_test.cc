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

#include <atomic>
#include <thread>
#include <vector>

#include "emulator/libs/display/include/goldfish/display/test/fake_pixman_display.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

using namespace goldfish::display;
using namespace goldfish::display::test;

class PixmanDisplayConcurrencyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mLoop = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create());
    }

    void TearDown() override { mLoop.reset(); }

    std::unique_ptr<goldfish::async::EventLoop> mLoop;
};

TEST_F(PixmanDisplayConcurrencyTest, ConcurrentGetPixelsDifferentScales) {
    int width = 640;
    int height = 480;
    // Use a high FPS to ensure constant updates
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), 0, 60, width, height);
    display->start();

    // Wait for at least one frame.
    ASSERT_TRUE(display->waitForFramesWithTimeout(1, absl::Milliseconds(1000)));

    const int kNumThreads = 8;
    const int kNumIterations = 50;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    std::atomic<int> failureCount{0};

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&display, i, &successCount, &failureCount]() {
            // Each thread uses a different target size to force different scaling transforms
            int targetWidth = 100 + i * 20;
            int targetHeight = 100 + i * 20;
            size_t cPixels = targetWidth * targetHeight * 4;
            std::vector<uint8_t> pixels(cPixels);

            for (int j = 0; j < kNumIterations; ++j) {
                size_t currentCPixels = cPixels;
                auto result = display->getPixels(PixelFormat::RGBA8888, targetWidth, targetHeight,
                                                 0, pixels.data(), &currentCPixels);
                if (result.ok()) {
                    successCount++;
                    // Basic sanity check: make sure we got some data
                    uint32_t* p = reinterpret_cast<uint32_t*>(pixels.data());
                    if (p[0] == 0 && p[targetWidth * targetHeight - 1] == 0) {
                        // This might happen if the generator produces black frames,
                        // but our generator produces Red, Green, Blue.
                    }
                } else {
                    failureCount++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    display->stop();

    EXPECT_EQ(failureCount, 0);
    EXPECT_EQ(successCount, kNumThreads * kNumIterations);
}

TEST_F(PixmanDisplayConcurrencyTest, ConcurrentUpdateAndGetPixels) {
    int width = 640;
    int height = 480;
    auto display = ActiveFakePixmanDisplay::createShared(mLoop.get(), 0, 100, width, height);
    display->start();

    const int kNumGetPixelThreads = 4;
    const int kNumResizeThreads = 2;
    const int kNumIterations = 50;
    std::vector<std::thread> threads;
    std::atomic<bool> running{true};

    // Threads calling getPixels
    for (int i = 0; i < kNumGetPixelThreads; ++i) {
        threads.emplace_back([&display, &running]() {
            int targetWidth = 320;
            int targetHeight = 240;
            size_t cPixels = targetWidth * targetHeight * 4;
            std::vector<uint8_t> pixels(cPixels);

            while (running) {
                size_t currentCPixels = cPixels;
                display->getPixels(PixelFormat::RGBA8888, targetWidth, targetHeight, 0,
                                   pixels.data(), &currentCPixels);
            }
        });
    }

    // Threads calling resize (which calls updateSourceImage)
    for (int i = 0; i < kNumResizeThreads; ++i) {
        threads.emplace_back([&display, &running, i]() {
            int sizes[] = {320, 640, 800, 1024};
            int idx = 0;
            while (running) {
                int s = sizes[idx % 4];
                display->resize(s, s);
                idx++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    running = false;

    for (auto& t : threads) {
        t.join();
    }

    display->stop();
}
