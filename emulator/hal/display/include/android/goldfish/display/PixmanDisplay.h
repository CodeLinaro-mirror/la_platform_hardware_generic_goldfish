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

#include "android/goldfish/display/Display.h"  // Include the IDisplay definition

extern "C" {
#include "pixman.h"
#include "qemu/osdep.h"
}

namespace android::goldfish {

// Custom deleter for pixman_image_t* to use with std::unique_ptr
struct PixmanImageDeleter {
    void operator()(::pixman_image_t* image) const {
        if (image) {
            ::pixman_image_unref(image);
        }
    }
};

// Type alias for a unique_ptr that manages a pixman_image_t*
using PixmanImagePtr = std::unique_ptr<::pixman_image_t, PixmanImageDeleter>;

class PixmanDisplay : public IDisplay {
  public:
    PixmanDisplay(int id, ::pixman_image_t* image);
    virtual ~PixmanDisplay() = default;

    virtual void updateSourceImage(::pixman_image_t* image);
    absl::StatusOr<FrameInfo> getPixels(PixelFormat format, int newWidth, int newHeight,
                                        int rotation, uint8_t* pixels,
                                        size_t* cPixels) const override;
    void updateSurface(int x, int y, int width, int height);

  protected:
    mutable absl::Mutex mDisplayAccess;
    PixmanImagePtr mSourceImage;
};

}  // namespace android::goldfish
