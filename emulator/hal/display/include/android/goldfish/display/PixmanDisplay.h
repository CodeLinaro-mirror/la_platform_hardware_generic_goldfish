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

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_format.h"

#include "android/goldfish/display/Display.h"  // Include the IDisplay definition

extern "C" {
#include "pixman.h"
#include "qemu/osdep.h"
}

namespace android::goldfish {

/**
 * @brief A smart pointer class for managing pixman_image_t* objects.
 *
 * This class provides automatic reference counting for pixman_image_t*
 * objects using `pixman_image_ref` and `pixman_image_unref`. It ensures
 * that the reference count is properly managed, preventing memory leaks
 * and double-frees.
 *
 * The class is designed to be used as a replacement for raw
 * pixman_image_t* pointers, providing RAII (Resource Acquisition Is
 * Initialization) semantics.
 */
class PixmanImagePtr {
  public:
    PixmanImagePtr(::pixman_image_t* image = nullptr);
    ~PixmanImagePtr();
    PixmanImagePtr(PixmanImagePtr&& other) noexcept;
    PixmanImagePtr& operator=(PixmanImagePtr&& other) noexcept;

    ::pixman_image_t* get() const;
    ::pixman_image_t* operator->() const;

  private:
    ::pixman_image_t* mImage;
};

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
    template <typename Sink>
    friend void AbslStringify(Sink&, const PixmanDisplay&);
    mutable absl::Mutex mDisplayAccess;
    PixmanImagePtr mSourceImage ABSL_GUARDED_BY(mDisplayAccess);
};

template <typename Sink>
void AbslStringify(Sink& sink, const PixmanDisplay& display) {
    absl::Format(&sink, "%s, src: %p", display.string(), display.mSourceImage.get());
}

}  // namespace android::goldfish
