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

#include "emulator/libs/display/include/goldfish/display/test/pixman_image_generator.h"

#include <chrono>
#include <iostream>
#include <thread>

extern "C" {
#include "pixman.h"
#include "qemu/osdep.h"
}

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::display::test {

namespace {
uint32_t GetColorValue(Color color) {
    switch (color) {
    case Color::kRed:
        return 0xFFFF0000;
    case Color::kGreen:
        return 0xFF00FF00;
    case Color::kBlue:
        return 0xFF0000FF;
    default:
        return 0xFF000000;  // Default to black if unknown
    }
}
}  // namespace

PixmanImageGenerator::PixmanImageGenerator(int fps, int width, int height)
        : fps_(fps), width_(width), height_(height), running_(false) {}

PixmanImageGenerator::~PixmanImageGenerator() {
    Stop();
}

void PixmanImageGenerator::Start() {
    if (running_) return;
    running_ = true;
    thread_ = std::make_unique<std::thread>([this] { GenerateImagesLoop(); });
}

void PixmanImageGenerator::Stop() {
    if (!running_) return;
    running_ = false;
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    VLOG(1) << "Clearing out thread";
    thread_.reset();
}

void PixmanImageGenerator::Resize(int w, int h) {
    const absl::MutexLock lock(&mutex_);
    width_ = w;
    height_ = h;
}

PixmanImagePtr PixmanImageGenerator::GenerateImage(Color color) {
    const absl::MutexLock lock(&mutex_);
    auto* pixels = new uint32_t[static_cast<size_t>(width_) * height_];
    const uint32_t color_value = GetColorValue(color);
    for (int i = 0; i < width_ * height_; ++i) {
        pixels[i] = color_value;
    }
    ::pixman_image_t* image = pixman_image_create_bits(PIXMAN_a8r8g8b8, width_, height_, pixels,
                                                       static_cast<int>(width_ * sizeof(uint32_t)));
    pixman_image_set_destroy_function(
            image,
            [](pixman_image_t* /*image*/, void* data) { delete[] static_cast<uint32_t*>(data); },
            pixels);
    return PixmanImagePtr(image);
}

bool PixmanImageGenerator::WaitForFramesWithTimeout(int n, absl::Duration timeout) {
    const absl::MutexLock lock(&mutex_);
    while (frame_count_ < n) {
        if (frame_cv_.WaitWithTimeout(&mutex_, timeout)) {
            return false;
        }
    }
    return true;
}

int PixmanImageGenerator::FrameCount() const {
    const absl::MutexLock lock(&mutex_);
    return frame_count_;
}

void PixmanImageGenerator::GenerateImagesLoop() {
    const std::chrono::milliseconds frame_duration(1000 / fps_);

    while (running_) {
        auto start = std::chrono::steady_clock::now();

        Color color;
        {
            const absl::MutexLock lock(&mutex_);
            switch (frame_count_ % 3) {
            case 0:
                color = Color::kRed;
                break;
            case 1:
                color = Color::kGreen;
                break;
            case 2:
                color = Color::kBlue;
                break;
            default:
                color = Color::kRed;  // Should not happen
                break;
            }
        }

        FireEvent(GenerateImage(color));

        {
            const absl::MutexLock lock(&mutex_);
            frame_count_++;
            frame_cv_.SignalAll();
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        } else {
            LOG(WARNING) << "Frame generation took longer than expected: " << elapsed.count()
                         << "ms";
        }
    }
}

}  // namespace goldfish::display::test
