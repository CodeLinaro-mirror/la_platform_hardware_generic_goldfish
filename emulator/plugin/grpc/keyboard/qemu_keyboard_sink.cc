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
#include "android/emulation/control/keyboard/qemu_keyboard_sink.h"

#include <string>

#include "absl/log/check.h"

#include "android/emulation/control/keyboard/key_conversion.h"
#include "key_event_emitter.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "ui/kbd-state.h"
}
// IWYU pragma: end_keep
// clang-format on

namespace android::emulation::control::keyboard {

QemuKeyboardSink::QemuKeyboardSink(QemuConsole* console, ::goldfish::async::EventLoop& qemu_loop)
        : qemu_loop_(qemu_loop), keyboard_state_(qkbd_state_init(console)) {}

QemuKeyboardSink::~QemuKeyboardSink() {
    qkbd_state_free(keyboard_state_);
}

void QemuKeyboardSink::WriteKey(uint32_t evdev_code, bool down) {
    DCHECK(qemu_loop_.IsOnLoopThread());
    QemuKeyEvent{evdev_to_qcode(evdev_code), down}.send(keyboard_state_);
}

void QemuKeyboardSink::TypeText(std::string_view utf8) {
    DCHECK(qemu_loop_.IsOnLoopThread());
    EmitUtf8String(std::string(utf8), [this](QemuKeyEvent event) { event.send(keyboard_state_); });
}

}  // namespace android::emulation::control::keyboard
