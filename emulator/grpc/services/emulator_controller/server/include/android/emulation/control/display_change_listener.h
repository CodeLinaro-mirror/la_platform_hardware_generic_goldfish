// Copyright (C) 2024 The Android Open Source Project
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
#include "android/emulation/control/grpc_event_stream_support.h"
#include "emulator_controller.pb.h"

namespace android {
namespace emulation {
namespace control {

/**
 * @brief Event listener for image changes.
 *
 * Objects can register themselves as listeners to receive events when a new
 * image is available.
 *
 */
class DisplayChangeListener {
  public:
    virtual ~DisplayChangeListener() = default;

    /**
     * @brief Adds an event listener to this object.
     *
     * The listener will receive Image events when new images are available.
     * The image will be in the provided ImageFormat and of the display id in the
     * image format.
     *
     * @param listener A pointer to the listener object to add.
     * @param fmt The desired format of the image.
     */
    virtual EventChangeSupport<Image>* addListener(ImageFormat fmt) = 0;

    virtual Image getScreenshot(ImageFormat fmt) = 0;
};

}  // namespace control
}  // namespace emulation
}  // namespace android