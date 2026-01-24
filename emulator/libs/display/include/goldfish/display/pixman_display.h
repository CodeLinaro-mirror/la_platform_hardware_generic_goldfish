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

#include "goldfish/display/display.h"
#include "goldfish/display/pixman_frame_manager.h"
#include "goldfish/display/pixman_image_ptr.h"
#include "goldfish/fps_calculator.h"

namespace goldfish::display {

class PixmanDisplay : public IDisplay {
  public:
    PixmanDisplay(EventLoop* loop, int id, ::pixman_image_t* image);
    PixmanDisplay(EventLoop* loop, int id, PixmanImagePtr image);

    virtual void updateSourceImage(::pixman_image_t* image);
    absl::StatusOr<FrameInfo> getPixels(PixelFormat format, int newWidth, int newHeight,
                                        ImageRotation rotation, uint8_t* pixels,
                                        size_t* cPixels) const override;
    void updateSurface(int x, int y, int width, int height);

    std::pair<int, int> resizeKeepAspectRatio(int desiredWidth, int desiredHeight) override;

  protected:
    template <typename Sink>
    friend void AbslStringify(Sink&, const PixmanDisplay&);
    std::unique_ptr<PixmanFrameManager> mFrameManager;
    ::goldfish::FpsCalculator mFpsCalculator{30};
};

template <typename Sink>
void AbslStringify(Sink& sink, const PixmanDisplay& display) {
    absl::Format(&sink, "%s", display.string());
}

}  // namespace goldfish::display
