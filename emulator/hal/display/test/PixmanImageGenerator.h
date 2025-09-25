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

#include <thread>

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "aemu/base/events/EventSources.h"
#include "android/goldfish/display/PixmanImagePtr.h"

namespace android::goldfish {

using android::base::eventing::CallbackEventSource;

enum class Color { Red, Green, Blue };

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
  void start();

  /**
   * @brief Stops the image generation process.
   */
  void stop();

  /**
   * @brief Resizes the generated images.
   *
   * @param w The new width.
   * @param h The new height.
   */
  void resize(int w, int h);

  /**
   * @brief Generates a pixman image with the specified color.
   */
  PixmanImagePtr generateImage(Color color);

  /**
   * @brief Waits for a specific number of frames to be generated with a timeout.
   *
   * @param n The number of frames to wait for.
   * @param timeout The maximum time to wait.
   * @return True if the desired number of frames were generated within the timeout, false
   * otherwise.
   */
  bool waitForFramesWithTimeout(int n, absl::Duration timeout);

  /**
   * @brief Returns the number of frames that have been generated.
   *
   * @return The number of frames generated.
   */
  int frameCount() const;

 private:
  void generateImagesLoop();

  int mFps;
  int mWidth ABSL_GUARDED_BY(mMutex);
  int mHeight ABSL_GUARDED_BY(mMutex);
  bool mRunning;
  std::unique_ptr<std::thread> mThread;
  mutable absl::Mutex mMutex;
  int mFrameCount ABSL_GUARDED_BY(mMutex);
  absl::CondVar mFrameCv;
};

}  // namespace android::goldfish
