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
#include "PixmanImageGenerator.h"

#include <chrono>
#include <iostream>
#include <thread>

extern "C" {
#include "pixman.h"
#include "qemu/osdep.h"
}

#include "absl/log/log.h"

namespace android::goldfish {

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
    : mFps(fps), mWidth(width), mHeight(height), mRunning(false) {}

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
    mThread.reset();
}

pixman_image_t* PixmanImageGenerator::generateImage(Color color) {
    uint32_t* pixels = new uint32_t[mWidth * mHeight];
    uint32_t colorValue = getColorValue(color);
    for (int i = 0; i < mWidth * mHeight; ++i) {
        pixels[i] = colorValue;
    }
    return pixman_image_create_bits(PIXMAN_a8r8g8b8, mWidth, mHeight, pixels,
                                    mWidth * sizeof(uint32_t));
}

void PixmanImageGenerator::generateImagesLoop() {
    const std::chrono::milliseconds frameDuration(1000 / mFps);
    int frameCount = 0;

    while (mRunning) {
        auto start = std::chrono::steady_clock::now();

        pixman_image_t* image = nullptr;
        switch (frameCount % 3) {
            case 0:
                image = generateImage(Color::Red);
                break;
            case 1:
                image = generateImage(Color::Green);
                break;
            case 2:
                image = generateImage(Color::Blue);
                break;
        }

        if (image) {
            fireEvent(image);
        }

        frameCount++;

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

}  // namespace android::goldfish
