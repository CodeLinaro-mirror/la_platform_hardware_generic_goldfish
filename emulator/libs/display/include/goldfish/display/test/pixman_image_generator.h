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

#include <cstdint>
#include <thread>

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "goldfish/display/pixman_image_ptr.h"
#include "goldfish/eventing/event_sources.h"

namespace goldfish::display::test {

using android::base::eventing::CallbackEventSource;

enum class Color : std::uint8_t { kRed, kGreen, kBlue };

/**
 * @brief Generates a sequence of red, green, and blue images at a specified frame rate.
 *
 * This class creates pixman images and notifies listeners whenever a new image is generated.
 */
class PixmanImageGenerator : public CallbackEventSource<PixmanImagePtr> {
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
    void Start();

    /**
     * @brief Stops the image generation process.
     */
    void Stop();

    /**
     * @brief Resizes the generated images.
     *
     * @param w The new width.
     * @param h The new height.
     */
    void Resize(int w, int h);

    /**
     * @brief Generates a pixman image with the specified color.
     */
    PixmanImagePtr GenerateImage(Color color);

    /**
     * @brief Waits for a specific number of frames to be generated with a timeout.
     *
     * @param n The number of frames to wait for.
     * @param timeout The maximum time to wait.
     * @return True if the desired number of frames were generated within the timeout, false
     * otherwise.
     */
    bool WaitForFramesWithTimeout(int n, absl::Duration timeout);

    /**
     * @brief Returns the number of frames that have been generated.
     *
     * @return The number of frames generated.
     */
    int FrameCount() const;

  private:
    void GenerateImagesLoop();

    int fps_;
    int width_ ABSL_GUARDED_BY(mutex_);
    int height_ ABSL_GUARDED_BY(mutex_);
    std::atomic_bool running_;
    std::unique_ptr<std::thread> thread_;
    mutable absl::Mutex mutex_;
    int frame_count_ ABSL_GUARDED_BY(mutex_) = 0;
    absl::CondVar frame_cv_;
};

}  // namespace goldfish::display::test
