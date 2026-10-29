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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include "android/emulation/control/keyboard/key_conversion.h"
#include "android/emulation/control/keyboard/key_event_sender.h"
#include "emulator_controller.pb.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "standard-headers/linux/input-event-codes.h"

namespace {

struct SentKey {
    QKeyCode code;
    bool down;

    friend bool operator==(const SentKey& a, const SentKey& b) {
        return a.code == b.code && a.down == b.down;
    }
    friend std::ostream& operator<<(std::ostream& os, const SentKey& key) {
        return os << "{qcode " << key.code << (key.down ? " down}" : " up}");
    }
};

// The key events sent to QEMU's keyboard state, in order.
std::vector<SentKey> g_sent_keys;
// The number of times QEMU's keyboard state was freed.
int g_frees = 0;

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
    if (kbd == FakeKeyboardState()) {
        ++g_frees;
    }
}

void qkbd_state_key_event(  // NOLINT(readability-identifier-naming)
        QKbdState* kbd, QKeyCode qcode, bool down) {
    if (kbd == FakeKeyboardState()) {
        g_sent_keys.push_back({qcode, down});
    }
}

// Maps only KEY_A, which is enough to see that WriteKey() translates its code.
int qemu_input_linux_to_qcode(unsigned int lnx) {
    return lnx == KEY_A ? Q_KEY_CODE_A : Q_KEY_CODE_UNMAPPED;
}
}

namespace android::emulation::control::keyboard {
namespace {

using ::goldfish::async::EventLoop;
using ::goldfish::async::testing::TestEventLoop;
using ::testing::ElementsAre;

class QemuKeyboardSinkTest : public ::testing::Test {
  protected:
    void SetUp() override {
        g_sent_keys.clear();
        g_frees = 0;
        sink_ = std::make_unique<QemuKeyboardSink>(/*console=*/nullptr, *loop_);
    }

    void TearDown() override {
        RunOnLoop([this] { sink_.reset(); });
    }

    // Runs `task` on the loop thread and waits for it to finish.
    void RunOnLoop(EventLoop::Task task) {
        ASSERT_TRUE(loop_->Post(std::move(task)).ok());
        loop_->RunAll();
    }

    std::unique_ptr<TestEventLoop> loop_ = TestEventLoop::Create();
    std::unique_ptr<QemuKeyboardSink> sink_;
};

TEST_F(QemuKeyboardSinkTest, WriteKeySendsOneKeyEventBeforeReturning) {
    std::vector<SentKey> sent_by_press;
    std::vector<SentKey> sent_by_release;
    RunOnLoop([&] {
        sink_->WriteKey(KEY_A, /*down=*/true);
        sent_by_press = g_sent_keys;
        sink_->WriteKey(KEY_A, /*down=*/false);
        sent_by_release = g_sent_keys;
    });

    EXPECT_THAT(sent_by_press, ElementsAre(SentKey{Q_KEY_CODE_A, true}));
    EXPECT_THAT(sent_by_release,
                ElementsAre(SentKey{Q_KEY_CODE_A, true}, SentKey{Q_KEY_CODE_A, false}));
}

TEST_F(QemuKeyboardSinkTest, TypeTextSendsTheKeyEventSenderSequenceBeforeReturning) {
    {
        auto sender = createKeyEventSender(/*console=*/nullptr, loop_.get());
        KeyboardEvent event;
        event.set_text("aB");
        sender->send(event);
    }
    loop_->RunAll();
    const std::vector<SentKey> posted = g_sent_keys;
    g_sent_keys.clear();

    std::vector<SentKey> typed;
    RunOnLoop([&] {
        sink_->TypeText("aB");
        typed = g_sent_keys;
    });

    EXPECT_EQ(typed, posted);
    EXPECT_THAT(typed, ElementsAre(SentKey{Q_KEY_CODE_A, true}, SentKey{Q_KEY_CODE_A, false},
                                   SentKey{Q_KEY_CODE_SHIFT, true}, SentKey{Q_KEY_CODE_B, true},
                                   SentKey{Q_KEY_CODE_B, false}, SentKey{Q_KEY_CODE_SHIFT, false}));
}

TEST_F(QemuKeyboardSinkTest, DestructorFreesTheKeyboardState) {
    int frees_by_destructor = 0;
    RunOnLoop([&] {
        sink_.reset();
        frees_by_destructor = g_frees;
    });

    EXPECT_EQ(frees_by_destructor, 1);
}

}  // namespace
}  // namespace android::emulation::control::keyboard
