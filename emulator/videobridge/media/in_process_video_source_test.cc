// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/videobridge/in_process_video_source.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>

#include "api/make_ref_counted.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/display/display.h"

namespace goldfish::videobridge {
namespace {

class FakeDisplay : public goldfish::display::IDisplay {
  public:
    FakeDisplay() : IDisplay(goldfish::async::globalEventLoop(), 0, 640, 480) {}

    void SetRotation(goldfish::display::ImageRotation rot) { rotation_ = rot; }

    absl::StatusOr<goldfish::display::FrameInfo> GetPixels(
            goldfish::display::PixelFormat /*fmt*/, int width, int height,
            goldfish::display::ImageRotation /*rotation*/, uint8_t* pixel,
            size_t* c_pixels) const override {
        size_t bytes = static_cast<size_t>(width) * height * 4;
        if (pixel && c_pixels && *c_pixels >= bytes) {
            std::memset(pixel, 128, bytes);
        }
        return goldfish::display::FrameInfo(1);
    }

    void SendMultiTouchEvent(uint8_t /*slot*/, int /*x*/, int /*y*/,
                             goldfish::display::MultiTouchType /*type*/) override {}
    void SendMouseEvent(int /*x*/, int /*y*/, int /*button_mask*/) override {}
    void SendEvDevEvent(uint16_t /*type*/, uint16_t /*code*/, uint32_t /*value*/) override {}

    void FireFrame() {
        fake_pixels_.assign(static_cast<size_t>(640) * 480 * 4, 128);
        goldfish::display::FrameInfo info(1);
        info.pixels = fake_pixels_.data();
        info.stride = static_cast<int>(640 * 4);
        info.dimensions = {.width = 640, .height = 480};
        info.format = goldfish::display::PixelFormat::kRgba8888;
        info.rotation = rotation_;
        goldfish::display::FrameInfoCallbackSource::FireEvent(info);
    }

  private:
    goldfish::display::ImageRotation rotation_ = goldfish::display::ImageRotation::kRotation0;
    mutable std::vector<uint8_t> fake_pixels_;
};

class DummyVideoSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
  public:
    void OnFrame(const webrtc::VideoFrame& frame) override {
        last_width = frame.width();
        last_height = frame.height();
        last_rotation = frame.rotation();
        frame_count++;
    }

    int frame_count = 0;
    int last_width = 0;
    int last_height = 0;
    webrtc::VideoRotation last_rotation = webrtc::kVideoRotation_0;
};

TEST(InProcessVideoSourceTest, StartCapturesFrameFromDisplay) {
    auto display = std::shared_ptr<FakeDisplay>(new FakeDisplay());
    auto source = webrtc::make_ref_counted<InProcessVideoSource>(display);
    DummyVideoSink sink;

    webrtc::VideoTrackSourceInterface* track_source = source.get();
    track_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants());
    source->Start();

    // Fire frame event on display
    display->FireFrame();

    // Wait up to 1s for event loop to dispatch callback
    for (int i = 0; i < 100 && sink.frame_count == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(sink.frame_count, 1);
    EXPECT_EQ(sink.last_width, 640);
    EXPECT_EQ(sink.last_height, 480);
    EXPECT_EQ(sink.last_rotation, webrtc::kVideoRotation_0);

    source->Stop();
    track_source->RemoveSink(&sink);
}

TEST(InProcessVideoSourceTest, PassesRotationToWebRtcFrame) {
    auto display = std::shared_ptr<FakeDisplay>(new FakeDisplay());
    display->SetRotation(goldfish::display::ImageRotation::kRotation90);
    auto source = webrtc::make_ref_counted<InProcessVideoSource>(display);
    DummyVideoSink sink;

    webrtc::VideoTrackSourceInterface* track_source = source.get();
    track_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants());
    source->Start();

    display->FireFrame();

    for (int i = 0; i < 100 && sink.frame_count == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(sink.frame_count, 1);
    EXPECT_EQ(sink.last_rotation, webrtc::kVideoRotation_90);

    source->Stop();
    track_source->RemoveSink(&sink);
}

}  // namespace
}  // namespace goldfish::videobridge
