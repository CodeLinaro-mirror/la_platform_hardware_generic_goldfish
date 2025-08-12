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
#include "FakePixmanDisplay.h"

#include "absl/log/log.h"

#include "android/goldfish/display/PixmanDisplay.h"

namespace android::goldfish {

FakePixmanDisplay::FakePixmanDisplay(int id, ::pixman_image_t* image) : PixmanDisplay(id, image) {}

void FakePixmanDisplay::sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    mMultiTouchEvents.push_back({slot, x, y, type});
}

void FakePixmanDisplay::updateSourceImage(::pixman_image_t* image) {
    absl::MutexLock lock(&mDisplayAccess);
    mSourceImage = PixmanImagePtr(image);
    auto oldWidth = mWidth;
    auto oldHeight = mHeight;
    mWidth = pixman_image_get_width(image);
    mHeight = pixman_image_get_height(image);

    // Notify listeners of updated display size,
    VLOG(1) << "updateSourceImage: " << *this;
    if (oldWidth != mWidth || oldHeight != mHeight) {
        VLOG(1) << "Informing listeners of change from " << oldWidth << "x" << oldHeight << " to "
                << mWidth << "x" << mHeight << "\n";
        EventChangeSupport<ResizeEvent>::fireEvent(
                ResizeEvent{mDisplayId, oldWidth, oldHeight, mWidth, mHeight});
    }
    updateSurface(0, 0, mWidth, mHeight);
}

void FakePixmanDisplay::sendMouseEvent(int x, int y, int button_mask) {
    mMouseEvents.push_back({x, y, button_mask});
}

void FakePixmanDisplay::sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) {
    mEvdevs.push_back({type, code, value});
}

ActiveFakePixmanDisplay::~ActiveFakePixmanDisplay() = default;

void ActiveFakePixmanDisplay::eventArrived(::pixman_image_t* image) {
    // Update the FakePixmanDisplay with the new image
    VLOG(1) << "Image arrived";
    updateSourceImage(image);
}

void ActiveFakePixmanDisplay::start() {
    mGenerator->start();
}

void ActiveFakePixmanDisplay::stop() {
    mGenerator->stop();
}

void ActiveFakePixmanDisplay::resize(int w, int h) {
    mGenerator->resize(w, h);
}

bool ActiveFakePixmanDisplay::waitForFramesWithTimeout(int n, absl::Duration timeout) {
    return mGenerator->waitForFramesWithTimeout(n, timeout);
}

ActiveFakePixmanDisplay ActiveFakePixmanDisplay::create(int id, int fps, int w, int h) {
    auto generator = std::make_unique<PixmanImageGenerator>(fps, w, h);
    return ActiveFakePixmanDisplay(id, std::move(generator));
}

std::shared_ptr<ActiveFakePixmanDisplay> ActiveFakePixmanDisplay::createShared(int id, int fps,
                                                                               int w, int h) {
    auto generator = std::make_unique<PixmanImageGenerator>(fps, w, h);
    return std::shared_ptr<ActiveFakePixmanDisplay>(
            new ActiveFakePixmanDisplay(id, std::move(generator)));
}

ActiveFakePixmanDisplay::ActiveFakePixmanDisplay(int id,
                                                 std::unique_ptr<PixmanImageGenerator> generator)
    : FakePixmanDisplay(id, generator->generateImage(Color::Blue)),
      mGenerator(std::move(generator)) {
    mGenerator->addListener(this);
}

}  // namespace android::goldfish
