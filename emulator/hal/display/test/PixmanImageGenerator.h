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
#pragma once

extern "C" {
#include <pixman.h>
}

#include <thread>

#include "android/emulation/control/utils/CallbackEventSupport.h"

namespace android::goldfish {

using android::emulation::control::EventChangeSupport;
using android::emulation::control::WithCallbacks;

enum class Color { Red, Green, Blue };

/**
 * @brief Generates a sequence of red, green, and blue images at a specified frame rate.
 *
 * This class creates pixman images and notifies listeners whenever a new image is generated.
 */
class PixmanImageGenerator : public WithCallbacks<EventChangeSupport, ::pixman_image_t*> {
  public:
    /**
     * @brief Constructs a PixmanImageGenerator.
     *
     * @param fps The frames per second at which to generate images.
     * @param width The width of the generated images.
     * @param height The height of the generated images.
     */
    PixmanImageGenerator(int fps, int width, int height);
    virtual ~PixmanImageGenerator();

    /**
     * @brief Starts the image generation process.
     */
    void start();

    /**
     * @brief Stops the image generation process.
     */
    void stop();

    /**
     * @brief Generates a pixman image with the specified color.
     */
    pixman_image_t* generateImage(Color color);

  private:
    void generateImagesLoop();

    int mFps;
    int mWidth;
    int mHeight;
    bool mRunning;
    std::unique_ptr<std::thread> mThread;
};

}  // namespace android::goldfish
