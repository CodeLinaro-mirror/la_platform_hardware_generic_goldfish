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

#include "goldfish/display/test/FakePixmanDisplay.h"

#include "absl/log/log.h"

namespace goldfish::display::test {

FakePixmanDisplay::FakePixmanDisplay(EventLoop* loop, int id, ::pixman_image_t* image)
        : PixmanDisplay(loop, id, image) {}

FakePixmanDisplay::FakePixmanDisplay(EventLoop* loop, int id, PixmanImagePtr image)
        : PixmanDisplay(loop, id, std::move(image)) {}

void FakePixmanDisplay::sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    mMultiTouchEvents.push_back({slot, x, y, type});
}

void FakePixmanDisplay::updateSourceImage(::pixman_image_t* image) {
    PixmanDisplay::updateSourceImage(image);
    updateSurface(0, 0, mWidth, mHeight);
}

void FakePixmanDisplay::sendMouseEvent(int x, int y, int button_mask) {
    mMouseEvents.push_back({x, y, button_mask});
}

void FakePixmanDisplay::sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) {
    mEvdevs.push_back({type, code, value});
}

ActiveFakePixmanDisplay::~ActiveFakePixmanDisplay() = default;

void ActiveFakePixmanDisplay::eventArrived(const PixmanImagePtr& image) {
    // Update the FakePixmanDisplay with the new image
    VLOG(1) << "Image arrived";
    updateSourceImage(image.get());
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

std::shared_ptr<ActiveFakePixmanDisplay> ActiveFakePixmanDisplay::createShared(EventLoop* loop,
                                                                               int id, int fps,
                                                                               int w, int h) {
    auto generator = std::make_unique<PixmanImageGenerator>(fps, w, h);
    auto fake = std::shared_ptr<ActiveFakePixmanDisplay>(
            new ActiveFakePixmanDisplay(loop, id, std::move(generator)));
    fake->mGenerator->addListener(fake);
    return fake;
}

ActiveFakePixmanDisplay::ActiveFakePixmanDisplay(EventLoop* loop, int id,
                                                 std::unique_ptr<PixmanImageGenerator> generator)
        : FakePixmanDisplay(loop, id, generator->generateImage(Color::Blue))
        , mGenerator(std::move(generator)) {}

}  // namespace goldfish::display::test
