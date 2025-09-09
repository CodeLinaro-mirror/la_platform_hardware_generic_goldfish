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

#include <memory>

#include "absl/synchronization/mutex.h"

#include "android/goldfish/display/PixmanImagePtr.h"
#include "pixman.h"

namespace android::goldfish {

/**
 * @brief Manages the frames that are being produced and consumed.
 *
 * This class implements a double buffering strategy to decouple the producer
 * from the consumer. It ensures that the consumer always has a complete and
 * valid frame to render, while the producer can update the next frame without
 * interfering with the rendering process.
 */
class PixmanFrameManager {
  public:
    /**
     * @brief Updates the source image that will be eventually consumed.
     *
     * This method is called by the producer to provide a new frame. The given
     * image will be staged and will become the current frame on the next call
     * to getRenderableImage().
     *
     * @param image A raw pointer to the new pixman image.
     */
    void updateSourceImage(::pixman_image_t* image);

    /**
     * @brief Returns a thread-safe image that can be safely rendered.
     *
     * This method is called by the consumer to get the latest complete frame.
     * It returns a smart pointer to the image, ensuring that the image is not
     * destroyed while it is being rendered.
     *
     * @return A PixmanImagePtr to the current frame.
     */
    PixmanImagePtr getRenderableImage();

  private:
    absl::Mutex mDisplayAccess;
    PixmanImagePtr mStagingImage ABSL_GUARDED_BY(mDisplayAccess);
    PixmanImagePtr mCurrentImage ABSL_GUARDED_BY(mDisplayAccess);
};

}  // namespace android::goldfish
