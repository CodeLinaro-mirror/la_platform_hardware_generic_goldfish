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

static uint32_t getColorValue(Color color) {
    switch (color) {
    case Color::Red:
        return 0xFFFF0000;
    case Color::Green:
        return 0xFF00FF00;
    case Color::Blue:
        return 0xFF0000FF;
    default:
        return 0xFF000000;  // Default to black if unknown
    }
}

PixmanImageGenerator::PixmanImageGenerator(int fps, int width, int height)
        : mFps(fps), mWidth(width), mHeight(height), mRunning(false), mFrameCount(0) {}

PixmanImageGenerator::~PixmanImageGenerator() {
    stop();
}

void PixmanImageGenerator::start() {
    if (mRunning) return;
    mRunning = true;
    mThread = std::make_unique<std::thread>([this] { generateImagesLoop(); });
}

void PixmanImageGenerator::stop() {
    if (!mRunning) return;
    mRunning = false;
    if (mThread && mThread->joinable()) {
        mThread->join();
    }
    fprintf(stderr, "Clearing out thread\n");
    mThread.reset();
}

void PixmanImageGenerator::resize(int w, int h) {
    absl::MutexLock lock(&mMutex);
    mWidth = w;
    mHeight = h;
}

PixmanImagePtr PixmanImageGenerator::generateImage(Color color) {
    absl::MutexLock lock(&mMutex);
    uint32_t* pixels = new uint32_t[mWidth * mHeight];
    uint32_t colorValue = getColorValue(color);
    for (int i = 0; i < mWidth * mHeight; ++i) {
        pixels[i] = colorValue;
    }
    ::pixman_image_t* image = pixman_image_create_bits(PIXMAN_a8r8g8b8, mWidth, mHeight, pixels,
                                                       mWidth * sizeof(uint32_t));
    pixman_image_set_destroy_function(
            image, [](pixman_image_t* image, void* data) { delete[] static_cast<uint32_t*>(data); },
            pixels);
    return PixmanImagePtr(image);
}

bool PixmanImageGenerator::waitForFramesWithTimeout(int n, absl::Duration timeout) {
    absl::MutexLock lock(&mMutex);
    while (mFrameCount < n) {
        if (mFrameCv.WaitWithTimeout(&mMutex, timeout)) {
            return false;
        }
    }
    return true;
}

int PixmanImageGenerator::frameCount() const {
    absl::MutexLock lock(&mMutex);
    return mFrameCount;
}

void PixmanImageGenerator::generateImagesLoop() {
    const std::chrono::milliseconds frameDuration(1000 / mFps);

    while (mRunning) {
        auto start = std::chrono::steady_clock::now();

        Color color;
        {
            absl::MutexLock lock(&mMutex);
            switch (mFrameCount % 3) {
            case 0:
                color = Color::Red;
                break;
            case 1:
                color = Color::Green;
                break;
            case 2:
                color = Color::Blue;
                break;
            }
        }

        FireEvent(generateImage(color));

        {
            absl::MutexLock lock(&mMutex);
            mFrameCount++;
            mFrameCv.SignalAll();
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        } else {
            LOG(WARNING) << "Frame generation took longer than expected: " << elapsed.count()
                         << "ms";
        }
    }
}

}  // namespace goldfish::display::test
