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

#include "android/emulation/control/in_process_rtc_service.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "absl/log/log.h"

#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/audio/qemu_audio_capture.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/display/test/fake_multi_display.h"
#include "goldfish/display/test/fake_pixman_display.h"

extern "C" {
struct QemuConsole {
    int dummy;
};
// NOLINTNEXTLINE(readability-identifier-naming)
QemuConsole* qemu_console_lookup_by_index(int index) {
    return nullptr;
}
}

namespace android::emulation::control {
namespace {

class TestAvdUniverse : public ::goldfish::avd_info::AvdUniverse {
  public:
    explicit TestAvdUniverse(::goldfish::display::IMultiDisplay* multi_display,
                             ::goldfish::async::EventLoop& loop)
            : ::goldfish::avd_info::AvdUniverse(
                      std::make_unique<::goldfish::avd_info::AvdProperties>())
            , multi_display_(multi_display)
            , loop_(loop) {}

    ::goldfish::async::EventLoop& GetQemuEventLoop() override { return loop_; }
    ::goldfish::metrics::MetricsReporter& GetMetricsReporter() override {
        LOG(FATAL) << "GetMetricsReporter not implemented in tests";
    }
    ::goldfish::display::IMultiDisplay& GetMultiDisplay() const override { return *multi_display_; }

    absl::Status OnSave(::goldfish::archive::IWriter&) const override { return absl::OkStatus(); }
    absl::Status OnLoad(::goldfish::archive::IReader&) override { return absl::OkStatus(); }

  private:
    ::goldfish::display::IMultiDisplay* multi_display_;
    ::goldfish::async::EventLoop& loop_;
};

#undef send
class FakeKeyEventSender : public keyboard::IKeyEventSender {
  public:
    void send(const KeyboardEvent request) override { sent_events.push_back(request); }

    std::vector<KeyboardEvent> sent_events;
};

TEST(InProcessRtcServiceTest, CreatesServiceFromAvdUniverse) {
    ::goldfish::display::test::FakeMultiDisplay fake_multidisplay(
            ::goldfish::async::globalEventLoop());
    TestAvdUniverse universe(&fake_multidisplay, *::goldfish::async::globalEventLoop());

    auto service = CreateInProcessRtcService(universe, 0, 0);
    ASSERT_NE(service, nullptr);
}

TEST(InProcessRtcServiceTest, InputSenderDispatchesKeyEvent) {
    ::goldfish::display::test::FakeMultiDisplay fake_multidisplay(
            ::goldfish::async::globalEventLoop());
    TestAvdUniverse universe(&fake_multidisplay, *::goldfish::async::globalEventLoop());
    auto key_event_sender = std::make_shared<FakeKeyEventSender>();
    auto factory = CreateInProcessInputSenderFactory(fake_multidisplay, key_event_sender);
    auto sender = factory(DataChannelLabel::kInput);
    ASSERT_NE(sender, nullptr);
    EXPECT_TRUE(sender->Start().ok());

    InputEvent event;
    event.mutable_key_event()->set_key("a");
    sender->SendEvent(event);

    ASSERT_EQ(key_event_sender->sent_events.size(), 1);
    EXPECT_EQ(key_event_sender->sent_events[0].key(), "a");
}

TEST(InProcessRtcServiceTest, InputSenderDispatchesTouchAndMouseEvent) {
    ::goldfish::display::test::FakeMultiDisplay fake_multidisplay(
            ::goldfish::async::globalEventLoop());
    TestAvdUniverse universe(&fake_multidisplay, *::goldfish::async::globalEventLoop());
    auto key_event_sender = std::make_shared<FakeKeyEventSender>();
    auto factory = CreateInProcessInputSenderFactory(fake_multidisplay, key_event_sender);
    auto sender = factory(DataChannelLabel::kInput);
    ASSERT_NE(sender, nullptr);
    EXPECT_TRUE(sender->Start().ok());

    InputEvent touch_event;
    auto* touch = touch_event.mutable_touch_event()->add_touches();
    touch->set_x(100);
    touch->set_y(200);
    touch->set_identifier(0);
    touch->set_pressure(100);
    sender->SendEvent(touch_event);

    InputEvent mouse_event;
    mouse_event.mutable_mouse_event()->set_x(50);
    mouse_event.mutable_mouse_event()->set_y(60);
    sender->SendEvent(mouse_event);

    auto display = fake_multidisplay.GetDisplay<::goldfish::display::test::ActiveFakePixmanDisplay>(
            fake_multidisplay.GetDisplay(0));

    EXPECT_GT(display->evdevs.size(), 0);
    bool has_touch = false;
    for (const auto& ev : display->evdevs) {
        if (ev.type == 3) has_touch = true;  // EV_ABS
    }
    EXPECT_TRUE(has_touch);

    ASSERT_EQ(display->mouse_events.size(), 1);
    EXPECT_EQ(display->mouse_events[0].x, 50);
    EXPECT_EQ(display->mouse_events[0].y, 60);
}

}  // namespace
}  // namespace android::emulation::control
