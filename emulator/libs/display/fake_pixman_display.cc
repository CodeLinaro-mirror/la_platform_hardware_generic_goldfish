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

#include "emulator/libs/display/include/goldfish/display/test/fake_pixman_display.h"

#include "absl/log/log.h"

namespace goldfish::display::test {

FakePixmanDisplay::FakePixmanDisplay(EventLoop* loop, int id, ::pixman_image_t* image)
        : PixmanDisplay(loop, id, image) {}

FakePixmanDisplay::FakePixmanDisplay(EventLoop* loop, int id, const PixmanImagePtr& image)
        : PixmanDisplay(loop, id, image) {}

void FakePixmanDisplay::SendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    multi_touch_events.push_back({slot, x, y, type});
}

void FakePixmanDisplay::UpdateSourceImage(::pixman_image_t* image) {
    PixmanDisplay::UpdateSourceImage(image);
    const Dimensions dims = GetDimensions();
    UpdateSurface(0, 0, static_cast<int>(dims.width), static_cast<int>(dims.height));
}

void FakePixmanDisplay::SendMouseEvent(int x, int y, int button_mask) {
    mouse_events.push_back({x, y, button_mask});
}

void FakePixmanDisplay::SendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) {
    evdevs.push_back({type, code, value});
}

ActiveFakePixmanDisplay::~ActiveFakePixmanDisplay() = default;

void ActiveFakePixmanDisplay::EventArrived(const PixmanImagePtr& image) {
    // Update the FakePixmanDisplay with the new image
    VLOG(1) << "Image arrived";
    UpdateSourceImage(image.get());
}

void ActiveFakePixmanDisplay::Start() {
    generator_->Start();
}

void ActiveFakePixmanDisplay::Stop() {
    generator_->Stop();
}

void ActiveFakePixmanDisplay::Resize(int w, int h) {
    generator_->Resize(w, h);
}

bool ActiveFakePixmanDisplay::WaitForFramesWithTimeout(int n, absl::Duration timeout) {
    return generator_->WaitForFramesWithTimeout(n, timeout);
}

std::shared_ptr<ActiveFakePixmanDisplay> ActiveFakePixmanDisplay::CreateShared(EventLoop* loop,
                                                                               int id, int fps,
                                                                               int w, int h) {
    auto generator = std::make_unique<PixmanImageGenerator>(fps, w, h);
    auto fake = std::shared_ptr<ActiveFakePixmanDisplay>(
            new ActiveFakePixmanDisplay(loop, id, std::move(generator)));
    fake->generator_->AddListener(fake);
    return fake;
}

ActiveFakePixmanDisplay::ActiveFakePixmanDisplay(EventLoop* loop, int id,
                                                 std::unique_ptr<PixmanImageGenerator> generator)
        : FakePixmanDisplay(loop, id, generator->GenerateImage(Color::kBlue))
        , generator_(std::move(generator)) {}

}  // namespace goldfish::display::test
