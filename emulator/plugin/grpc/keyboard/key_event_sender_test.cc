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

#include "android/emulation/control/keyboard/key_event_sender.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "android/emulation/control/keyboard/key_conversion.h"
#include "emulator_controller.pb.h"
#include "goldfish/async/testing/test_event_loop.h"

namespace {

// Records the QEMU keyboard state calls made by the sender, in order.
std::vector<std::string> g_kbd_calls;

QKbdState* FakeKeyboardState() {
    static int storage;
    return reinterpret_cast<QKbdState*>(&storage);
}

}  // namespace

// Recording replacements for the QEMU input functions the keyboard libraries link against.
extern "C" {
QKbdState* qkbd_state_init(QemuConsole* /*con*/) {  // NOLINT(readability-identifier-naming)
    return FakeKeyboardState();
}

void qkbd_state_free(QKbdState* kbd) {  // NOLINT(readability-identifier-naming)
    g_kbd_calls.push_back(kbd == FakeKeyboardState() ? "free" : "free(unknown)");
}

void qkbd_state_key_event(  // NOLINT(readability-identifier-naming)
        QKbdState* kbd, QKeyCode /*qcode*/, bool /*down*/) {
    g_kbd_calls.push_back(kbd == FakeKeyboardState() ? "key" : "key(unknown)");
}

int qemu_input_linux_to_qcode(unsigned int /*lnx*/) {
    return 0;
}
}

namespace android::emulation::control::keyboard {
namespace {

using ::goldfish::async::testing::TestEventLoop;
using ::testing::ElementsAre;

class KeyEventSenderTest : public ::testing::Test {
  protected:
    void SetUp() override { g_kbd_calls.clear(); }

    std::unique_ptr<TestEventLoop> loop_ = TestEventLoop::Create();
};

TEST_F(KeyEventSenderTest, QueuedKeyEventsRunBeforeKeyboardStateIsFreed) {
    {
        auto sender = createKeyEventSender(/*console=*/nullptr, loop_.get());
        KeyboardEvent event;
        event.set_codetype(KeyboardEvent::Evdev);
        event.set_eventtype(KeyboardEvent::keydown);
        event.set_keycode(30);
        sender->send(event);
        // The sender goes away while its key event is still queued on the loop.
    }
    loop_->RunAll();

    EXPECT_THAT(g_kbd_calls, ElementsAre("key", "free"));
}

TEST_F(KeyEventSenderTest, KeyboardStateIsFreedWithoutQueuedEvents) {
    {
        auto sender = createKeyEventSender(/*console=*/nullptr, loop_.get());
    }
    loop_->RunAll();

    EXPECT_THAT(g_kbd_calls, ElementsAre("free"));
}

}  // namespace
}  // namespace android::emulation::control::keyboard
