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
#include <vector>

#include "api/make_ref_counted.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/display/display.h"
#include "goldfish/display/test/fake_multi_display.h"
#include "goldfish/display/test/fake_pixman_display.h"

namespace goldfish::videobridge {
namespace {

struct DummyVideoSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
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

TEST(InProcessVideoSourceTest, StartCapturesFrameFromMultiDisplay) {
    goldfish::display::test::FakeMultiDisplay fake_multidisplay(goldfish::async::globalEventLoop());
    auto source = webrtc::make_ref_counted<InProcessVideoSource>(fake_multidisplay, 0);
    DummyVideoSink sink;

    webrtc::VideoTrackSourceInterface* track_source = source.get();
    track_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants());
    source->Start();

    auto display_res = fake_multidisplay.GetDisplay(0);
    ASSERT_TRUE(display_res.ok());
    auto display = fake_multidisplay.GetDisplay<goldfish::display::test::ActiveFakePixmanDisplay>(
            display_res);
    ASSERT_NE(display, nullptr);

    display->Start();
    display->WaitForFramesWithTimeout(2, absl::Milliseconds(1000));
    display->Stop();

    EXPECT_GT(sink.frame_count, 0);
    EXPECT_EQ(sink.last_width, 640);
    EXPECT_EQ(sink.last_height, 480);
    EXPECT_EQ(sink.last_rotation, webrtc::kVideoRotation_0);

    source->Stop();
    track_source->RemoveSink(&sink);
}

TEST(InProcessVideoSourceTest, HandlesDisplayCreatedAfterSourceInitialization) {
    goldfish::display::test::FakeMultiDisplay fake_multidisplay(goldfish::async::globalEventLoop());
    // Display 1 does not exist initially
    auto source = webrtc::make_ref_counted<InProcessVideoSource>(fake_multidisplay, 1);
    DummyVideoSink sink;

    webrtc::VideoTrackSourceInterface* track_source = source.get();
    track_source->AddOrUpdateSink(&sink, webrtc::VideoSinkWants());

    // Start before display is added (does not crash, remains idle)
    source->Start();
    source->Stop();

    // Create display 1 now (800x600)
    auto create_res = fake_multidisplay.CreateDisplay(1, 800, 600, 320, 0);
    ASSERT_TRUE(create_res.ok());
    auto display = fake_multidisplay.GetDisplay<goldfish::display::test::ActiveFakePixmanDisplay>(
            create_res);
    ASSERT_NE(display, nullptr);

    // Start streaming on display 1
    source->Start();

    display->Start();
    display->WaitForFramesWithTimeout(2, absl::Milliseconds(1000));
    display->Stop();

    EXPECT_GT(sink.frame_count, 0);
    EXPECT_EQ(sink.last_width, 800);
    EXPECT_EQ(sink.last_height, 600);

    source->Stop();
    track_source->RemoveSink(&sink);
}

}  // namespace
}  // namespace goldfish::videobridge
