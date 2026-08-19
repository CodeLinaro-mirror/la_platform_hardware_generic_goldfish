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

// External WebRTC headers included transitively by in_process_rtc_service.h
// lack complete nullability annotations, triggering Clang warnings without this pragma.
#pragma clang diagnostic ignored "-Wnullability-completeness"

#include "android/emulation/control/in_process_rtc_service.h"

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

#include "android/emulation/control/event_sender.h"
#include "api/make_ref_counted.h"
#include "goldfish/audio/qemu_audio_source.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/display/QemuMultidisplay/multi_display.h"
#include "goldfish/videobridge/in_process_input_sender.h"
#include "goldfish/videobridge/in_process_video_source.h"
#include "goldfish/videobridge/media_track_provider.h"
#include "goldfish/videobridge/rtc_service.h"
#include "goldfish/videobridge/switchboard.h"

extern "C" {
typedef struct QemuConsole QemuConsole;
// NOLINTNEXTLINE(readability-identifier-naming)
QemuConsole* qemu_console_lookup_by_index(int index);
// NOLINTNEXTLINE(readability-identifier-naming)
QemuConsole* qemu_console_lookup_default(void);
}

namespace android::emulation::control {

using ::goldfish::audio::QemuAudioSource;
using ::goldfish::avd_info::AvdUniverse;
using ::goldfish::display::IMultiDisplay;
using ::goldfish::videobridge::DataChannelLabel;
using ::goldfish::videobridge::InProcessInputSender;
using ::goldfish::videobridge::InProcessVideoSource;
using ::goldfish::videobridge::InputEvent;
using ::goldfish::videobridge::InputSender;
using ::goldfish::videobridge::InputSenderFactory;
using ::goldfish::videobridge::MediaTrackProvider;
using ::goldfish::videobridge::RtcService;
using ::goldfish::videobridge::Switchboard;
using keyboard::IKeyEventSender;

namespace {

// Clean, testable event routing isolated from factory plumbing.
absl::Status DispatchInProcessInputEvent(const InputEvent& request,
                                         InputEventSender& input_event_sender,
                                         IKeyEventSender& key_event_sender) {
    VLOG(2) << "InProcess InputEvent: " << request.ShortDebugString();

    switch (request.type_case()) {
    case InputEvent::kKeyEvent:
        key_event_sender.send(request.key_event());
        return absl::OkStatus();

    case InputEvent::kMouseEvent:
        return input_event_sender.Send(request.mouse_event());

    case InputEvent::kTouchEvent:
        return input_event_sender.Send(request.touch_event());

    case InputEvent::kAndroidEvent:
        return input_event_sender.Send(request.android_event());

    case InputEvent::kPenEvent:
        return input_event_sender.Send(request.pen_event());

    case InputEvent::kWheelEvent:
        return input_event_sender.Send(request.wheel_event());

    case InputEvent::TYPE_NOT_SET:
    default:
        return absl::InvalidArgumentError("Unknown or unset InputEvent payload");
    }
}

}  // namespace

InputSenderFactory CreateInProcessInputSenderFactory(
        IMultiDisplay& multidisplay, std::shared_ptr<IKeyEventSender> key_event_sender) {
    CHECK_NE(key_event_sender, nullptr) << "key_event_sender must not be null";
    auto input_event_sender = std::make_shared<InputEventSender>(&multidisplay);

    return [input_event_sender, key_event_sender = std::move(key_event_sender)](
                   DataChannelLabel /*label*/) -> std::unique_ptr<InputSender> {
        return std::make_unique<InProcessInputSender>([input_event_sender, key_event_sender](
                                                              const InputEvent& request) {
            return DispatchInProcessInputEvent(request, *input_event_sender, *key_event_sender);
        });
    };
}

std::shared_ptr<::grpc::Service> CreateInProcessRtcService(AvdUniverse& avd_universe,
                                                           uint32_t display_id,
                                                           uint32_t console_index,
                                                           AudioBackend* audio_backend) {
    LOG(INFO) << "Creating in-process WebRTC RtcService for display " << display_id;

    auto video_source = ::webrtc::make_ref_counted<InProcessVideoSource>(
            avd_universe.GetMultiDisplay(), display_id);

    // QemuAudioSource handles a null audio_backend gracefully by resolving the default active
    // backend.
    auto audio_source = ::webrtc::make_ref_counted<QemuAudioSource>(audio_backend);

    auto media_provider = std::make_shared<MediaTrackProvider>(video_source, audio_source);

    QemuConsole* console = qemu_console_lookup_by_index(console_index);
    if (console == nullptr) {
        LOG(WARNING) << "Failed to find QemuConsole for console index " << console_index
                     << ". Input injection will fallback to default console.";
        console = qemu_console_lookup_default();
    }

    // keyboard::createKeyEventSender explicitly supports a null QemuConsole* gracefully.
    std::shared_ptr<IKeyEventSender> key_event_sender =
            keyboard::createKeyEventSender(console, &avd_universe.GetQemuEventLoop());
    auto input_sender_factory = CreateInProcessInputSenderFactory(avd_universe.GetMultiDisplay(),
                                                                  std::move(key_event_sender));
    auto switchboard = std::make_shared<Switchboard>(media_provider, input_sender_factory);

    return std::make_shared<RtcService>(switchboard);
}

}  // namespace android::emulation::control
