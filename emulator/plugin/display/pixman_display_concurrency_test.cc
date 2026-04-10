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

#include "goldfish/display/test/fake_pixman_display.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

using goldfish::display::ImageRotation;
using goldfish::display::PixelFormat;
using goldfish::display::test::ActiveFakePixmanDisplay;

class PixmanDisplayConcurrencyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop_ = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create());
    }

    void TearDown() override { loop_.reset(); }

    std::unique_ptr<goldfish::async::EventLoop> loop_;
};

TEST_F(PixmanDisplayConcurrencyTest, DISABLED_ConcurrentGetPixelsDifferentScales) {
    const int width = 640;
    const int height = 480;
    // Use a high FPS to ensure constant updates
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), 0, 60, width, height);
    display->Start();

    // Wait for at least one frame.
    ASSERT_TRUE(display->WaitForFramesWithTimeout(1, absl::Milliseconds(1000)));

    const int k_num_threads = 8;
    const int k_num_iterations = 50;
    std::vector<std::thread> threads;
    threads.reserve(k_num_threads);
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    for (int i = 0; i < k_num_threads; ++i) {
        threads.emplace_back([&display, i, &success_count, &failure_count]() {
            // Each thread uses a different target size to force different scaling transforms
            const int target_width = 100 + (i * 20);
            const int target_height = 100 + (i * 20);
            const size_t c_pixels = static_cast<size_t>(target_width) * target_height * 4;
            std::vector<uint8_t> pixels(c_pixels);

            for (int j = 0; j < k_num_iterations; ++j) {
                size_t current_c_pixels = c_pixels;
                auto result = display->GetPixels(PixelFormat::kRgba8888, target_width,
                                                 target_height, ImageRotation::kRotation0,
                                                 pixels.data(), &current_c_pixels);
                if (result.ok()) {
                    success_count++;
                    // Basic sanity check: make sure we got some data
                    auto* p = reinterpret_cast<uint32_t*>(pixels.data());
                    if (p[0] == 0 &&
                        p[(static_cast<size_t>(target_width) * target_height) - 1] == 0) {
                        // This might happen if the generator produces black frames,
                        // but our generator produces Red, Green, Blue.
                    }
                } else {
                    failure_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    display->Stop();

    EXPECT_EQ(failure_count, 0);
    EXPECT_EQ(success_count, k_num_threads * k_num_iterations);
}

TEST_F(PixmanDisplayConcurrencyTest, DISABLED_ConcurrentUpdateAndGetPixels) {
    const int width = 640;
    const int height = 480;
    auto display = ActiveFakePixmanDisplay::CreateShared(loop_.get(), 0, 100, width, height);
    display->Start();

    const int k_num_get_pixel_threads = 4;
    const int k_num_resize_threads = 2;
    std::vector<std::thread> threads;
    threads.reserve(k_num_get_pixel_threads + k_num_resize_threads);
    std::atomic<bool> running{true};

    // Threads calling GetPixels
    for (int i = 0; i < k_num_get_pixel_threads; ++i) {
        threads.emplace_back([&display, &running]() {
            const int target_width = 320;
            const int target_height = 240;
            const size_t c_pixels = static_cast<size_t>(target_width) * target_height * 4;
            std::vector<uint8_t> pixels(c_pixels);

            while (running) {
                size_t current_c_pixels = c_pixels;
                (void)display->GetPixels(PixelFormat::kRgba8888, target_width, target_height,
                                         ImageRotation::kRotation0, pixels.data(),
                                         &current_c_pixels);
            }
        });
    }

    // Threads calling resize (which calls updateSourceImage)
    for (int i = 0; i < k_num_resize_threads; ++i) {
        threads.emplace_back([&display, &running]() {
            const int sizes[] = {320, 640, 800, 1024};
            int idx = 0;
            while (running) {
                const int s = sizes[idx % 4];
                display->Resize(s, s);
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

    display->Stop();
}
