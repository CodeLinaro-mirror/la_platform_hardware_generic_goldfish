// Copyright (C) 2025 The Android Open Source Project
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

#include <array>
#include <cstdint>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "android/emulation/control/keyboard/key_conversion.h"
#include "dom_key.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/async/event_loop.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/input.h"


// Forward declarations for qemu bindings.
typedef struct QKbdState QKbdState;
QKbdState* qkbd_state_init(QemuConsole* con);  // NOLINT(readability-identifier-naming)
void qkbd_state_free(QKbdState* kbd);        // NOLINT(readability-identifier-naming)

// IWYU pragma: end_keep
// clang-format on
}

namespace android {
namespace emulation {
namespace control {
namespace keyboard {

using goldfish::async::EventLoop;
struct DomKeyMapEntry {
    DomKey dom_key;
    const char* string;
};

#define DOM_KEY_MAP_DECLARATION const DomKeyMapEntry dom_key_map[] =
#define DOM_KEY_UNI(key, id, value) {DomKey::id, key}
#define DOM_KEY_MAP(key, id, value) {DomKey::id, key}
#include "dom_key_data.inc"

#undef DOM_KEY_MAP_DECLARATION
#undef DOM_KEY_MAP
#undef DOM_KEY_UNI

// Necessary on Windows.
#undef send

// From
// https://cs.chromium.org/chromium/src/ui/events/keycodes/dom_us_layout_data.h
const struct NonPrintableCodeEntry {
    DomCode dom_code;
    DomKey::Base dom_key;
} kNonPrintableCodeMap[] = {
    {DomCode::ABORT, DomKey::CANCEL},
    {DomCode::AGAIN, DomKey::AGAIN},
    {DomCode::ALT_LEFT, DomKey::ALT},
    {DomCode::ALT_RIGHT, DomKey::ALT},
    {DomCode::ARROW_DOWN, DomKey::ARROW_DOWN},
    {DomCode::ARROW_LEFT, DomKey::ARROW_LEFT},
    {DomCode::ARROW_RIGHT, DomKey::ARROW_RIGHT},
    {DomCode::ARROW_UP, DomKey::ARROW_UP},
    {DomCode::BACKSPACE, DomKey::BACKSPACE},
    {DomCode::BASS_BOOST, DomKey::AUDIO_BASS_BOOST_TOGGLE},
    {DomCode::BRIGHTNESS_DOWN, DomKey::BRIGHTNESS_DOWN},
    {DomCode::BRIGHTNESS_UP, DomKey::BRIGHTNESS_UP},
    // {DomCode::BRIGHTNESS_AUTO, DomKey::_}
    // {DomCode::BRIGHTNESS_MAXIMUM, DomKey::_}
    // {DomCode::BRIGHTNESS_MINIMIUM, DomKey::_}
    // {DomCode::BRIGHTNESS_TOGGLE, DomKey::_}
    {DomCode::BROWSER_BACK, DomKey::BROWSER_BACK},
    {DomCode::BROWSER_FAVORITES, DomKey::BROWSER_FAVORITES},
    {DomCode::BROWSER_FORWARD, DomKey::BROWSER_FORWARD},
    {DomCode::BROWSER_HOME, DomKey::BROWSER_HOME},
    {DomCode::BROWSER_REFRESH, DomKey::BROWSER_REFRESH},
    {DomCode::BROWSER_SEARCH, DomKey::BROWSER_SEARCH},
    {DomCode::BROWSER_STOP, DomKey::BROWSER_STOP},
    {DomCode::CAPS_LOCK, DomKey::CAPS_LOCK},
    {DomCode::CHANNEL_DOWN, DomKey::CHANNEL_DOWN},
    {DomCode::CHANNEL_UP, DomKey::CHANNEL_UP},
    {DomCode::CLOSE, DomKey::CLOSE},
    {DomCode::CLOSED_CAPTION_TOGGLE, DomKey::CLOSED_CAPTION_TOGGLE},
    {DomCode::CONTEXT_MENU, DomKey::CONTEXT_MENU},
    {DomCode::CONTROL_LEFT, DomKey::CONTROL},
    {DomCode::CONTROL_RIGHT, DomKey::CONTROL},
    {DomCode::CONVERT, DomKey::CONVERT},
    {DomCode::COPY, DomKey::COPY},
    {DomCode::CUT, DomKey::CUT},
    {DomCode::DEL, DomKey::DEL},
    {DomCode::EJECT, DomKey::EJECT},
    {DomCode::END, DomKey::END},
    {DomCode::ENTER, DomKey::ENTER},
    {DomCode::ESCAPE, DomKey::ESCAPE},
    {DomCode::EXIT, DomKey::EXIT},
    {DomCode::F1, DomKey::F1},
    {DomCode::F2, DomKey::F2},
    {DomCode::F3, DomKey::F3},
    {DomCode::F4, DomKey::F4},
    {DomCode::F5, DomKey::F5},
    {DomCode::F6, DomKey::F6},
    {DomCode::F7, DomKey::F7},
    {DomCode::F8, DomKey::F8},
    {DomCode::F9, DomKey::F9},
    {DomCode::F10, DomKey::F10},
    {DomCode::F11, DomKey::F11},
    {DomCode::F12, DomKey::F12},
    {DomCode::F13, DomKey::F13},
    {DomCode::F14, DomKey::F14},
    {DomCode::F15, DomKey::F15},
    {DomCode::F16, DomKey::F16},
    {DomCode::F17, DomKey::F17},
    {DomCode::F18, DomKey::F18},
    {DomCode::F19, DomKey::F19},
    {DomCode::F20, DomKey::F20},
    {DomCode::F21, DomKey::F21},
    {DomCode::F22, DomKey::F22},
    {DomCode::F23, DomKey::F23},
    {DomCode::F24, DomKey::F24},
    {DomCode::FIND, DomKey::FIND},
    {DomCode::FN, DomKey::FN},
    {DomCode::FN_LOCK, DomKey::FN_LOCK},
    {DomCode::HELP, DomKey::HELP},
    {DomCode::HOME, DomKey::HOME},
    {DomCode::HYPER, DomKey::HYPER},
    {DomCode::INFO, DomKey::INFO},
    {DomCode::INSERT, DomKey::INSERT},
    // {DomCode::INTL_RO, DomKey::_}
    {DomCode::KANA_MODE, DomKey::KANA_MODE},
    {DomCode::LANG1, DomKey::HANGUL_MODE},
    {DomCode::LANG2, DomKey::HANJA_MODE},
    {DomCode::LANG3, DomKey::KATAKANA},
    {DomCode::LANG4, DomKey::HIRAGANA},
    {DomCode::LANG5, DomKey::ZENKAKU_HANKAKU},
    {DomCode::LAUNCH_APP1, DomKey::LAUNCH_MY_COMPUTER},
    {DomCode::LAUNCH_APP2, DomKey::LAUNCH_CALCULATOR},
    {DomCode::LAUNCH_ASSISTANT, DomKey::LAUNCH_ASSISTANT},
    {DomCode::LAUNCH_AUDIO_BROWSER, DomKey::LAUNCH_MUSIC_PLAYER},
    {DomCode::LAUNCH_CALENDAR, DomKey::LAUNCH_CALENDAR},
    {DomCode::LAUNCH_CONTACTS, DomKey::LAUNCH_CONTACTS},
    {DomCode::LAUNCH_CONTROL_PANEL, DomKey::SETTINGS},
    {DomCode::LAUNCH_INTERNET_BROWSER, DomKey::LAUNCH_WEB_BROWSER},
    {DomCode::LAUNCH_MAIL, DomKey::LAUNCH_MAIL},
    {DomCode::LAUNCH_PHONE, DomKey::LAUNCH_PHONE},
    {DomCode::LAUNCH_SCREEN_SAVER, DomKey::LAUNCH_SCREEN_SAVER},
    {DomCode::LAUNCH_SPREADSHEET, DomKey::LAUNCH_SPREADSHEET},
    // {DomCode::LAUNCH_DOCUMENTS, DomKey::_}
    // {DomCode::LAUNCH_FILE_BROWSER, DomKey::_}
    // {DomCode::LAUNCH_KEYBOARD_LAYOUT, DomKey::_}
    {DomCode::LOCK_SCREEN, DomKey::LAUNCH_SCREEN_SAVER},
    {DomCode::LOG_OFF, DomKey::LOG_OFF},
    {DomCode::MAIL_FORWARD, DomKey::MAIL_FORWARD},
    {DomCode::MAIL_REPLY, DomKey::MAIL_REPLY},
    {DomCode::MAIL_SEND, DomKey::MAIL_SEND},
    {DomCode::MEDIA_FAST_FORWARD, DomKey::MEDIA_FAST_FORWARD},
    {DomCode::MEDIA_LAST, DomKey::MEDIA_LAST},
    // {DomCode::MEDIA_PAUSE, DomKey::MEDIA_PAUSE},
    {DomCode::MEDIA_PLAY, DomKey::MEDIA_PLAY},
    {DomCode::MEDIA_PLAY_PAUSE, DomKey::MEDIA_PLAY_PAUSE},
    {DomCode::MEDIA_RECORD, DomKey::MEDIA_RECORD},
    {DomCode::MEDIA_REWIND, DomKey::MEDIA_REWIND},
    {DomCode::MEDIA_SELECT, DomKey::LAUNCH_MEDIA_PLAYER},
    {DomCode::MEDIA_STOP, DomKey::MEDIA_STOP},
    {DomCode::MEDIA_TRACK_NEXT, DomKey::MEDIA_TRACK_NEXT},
    {DomCode::MEDIA_TRACK_PREVIOUS, DomKey::MEDIA_TRACK_PREVIOUS},
    // {DomCode::MENU, DomKey::_}
    {DomCode::NEW, DomKey::NEW},
    {DomCode::NON_CONVERT, DomKey::NON_CONVERT},
    {DomCode::NUM_LOCK, DomKey::NUM_LOCK},
    {DomCode::NUMPAD_BACKSPACE, DomKey::BACKSPACE},
    {DomCode::NUMPAD_CLEAR, DomKey::CLEAR},
    {DomCode::NUMPAD_ENTER, DomKey::ENTER},
    // {DomCode::NUMPAD_CLEAR_ENTRY, DomKey::_}
    // {DomCode::NUMPAD_MEMORY_ADD, DomKey::_}
    // {DomCode::NUMPAD_MEMORY_CLEAR, DomKey::_}
    // {DomCode::NUMPAD_MEMORY_RECALL, DomKey::_}
    // {DomCode::NUMPAD_MEMORY_STORE, DomKey::_}
    // {DomCode::NUMPAD_MEMORY_SUBTRACT, DomKey::_}
    {DomCode::OPEN, DomKey::OPEN},
    {DomCode::META_LEFT, DomKey::META},
    {DomCode::META_RIGHT, DomKey::META},
    {DomCode::PAGE_DOWN, DomKey::PAGE_DOWN},
    {DomCode::PAGE_UP, DomKey::PAGE_UP},
    {DomCode::PASTE, DomKey::PASTE},
    {DomCode::PAUSE, DomKey::PAUSE},
    {DomCode::POWER, DomKey::POWER},
    {DomCode::PRINT, DomKey::PRINT},
    {DomCode::PRINT_SCREEN, DomKey::PRINT_SCREEN},
    {DomCode::PROGRAM_GUIDE, DomKey::GUIDE},
    {DomCode::PROPS, DomKey::PROPS},
    {DomCode::REDO, DomKey::REDO},
    {DomCode::SAVE, DomKey::SAVE},
    {DomCode::SCROLL_LOCK, DomKey::SCROLL_LOCK},
    {DomCode::SELECT, DomKey::SELECT},
    {DomCode::SELECT_TASK, DomKey::APP_SWITCH},
    {DomCode::SHIFT_LEFT, DomKey::SHIFT},
    {DomCode::SHIFT_RIGHT, DomKey::SHIFT},
    {DomCode::SPEECH_INPUT_TOGGLE, DomKey::SPEECH_INPUT_TOGGLE},
    {DomCode::SPELL_CHECK, DomKey::SPELL_CHECK},
    {DomCode::SUPER, DomKey::SUPER},
    {DomCode::TAB, DomKey::TAB},
    {DomCode::UNDO, DomKey::UNDO},
    {DomCode::VOLUME_DOWN, DomKey::AUDIO_VOLUME_DOWN},
    {DomCode::VOLUME_MUTE, DomKey::AUDIO_VOLUME_MUTE},
    {DomCode::VOLUME_UP, DomKey::AUDIO_VOLUME_UP},
    {DomCode::WAKE_UP, DomKey::WAKE_UP},
    {DomCode::ZOOM_IN, DomKey::ZOOM_IN},
    {DomCode::ZOOM_OUT, DomKey::ZOOM_OUT},
    {DomCode::ZOOM_TOGGLE, DomKey::ZOOM_TOGGLE},

    // Android specific events.
    {DomCode::GO_BACK, DomKey::GO_BACK},        // AC Back in usb
    {DomCode::GO_HOME, DomKey::GO_HOME},        // Same as HOME
    {DomCode::APP_SWITCH, DomKey::APP_SWITCH},  // "Overview"
    // {DomCode::CAMERA, DomKey::CAMERA},          // Camera
    {DomCode::SLEEP, DomKey::STANDBY},  // Sleep
};

const size_t kDomKeyMapEntries = std::size(dom_key_map);
const size_t kNonPrintableCodeEntries = std::size(kNonPrintableCodeMap);

class KeyEventSenderImpl : public IKeyEventSender {
  public:
    KeyEventSenderImpl(QemuConsole* con, EventLoop* qemu_loop) : qemu_loop_(qemu_loop) {
        keyboard_state_ = qkbd_state_init(con);
    }
    ~KeyEventSenderImpl() { qkbd_state_free(keyboard_state_); }

    void send(const KeyboardEvent request) override { DispatchKeyboardEvent(request); }

  private:
    void SendKeyCode(int32_t code, const KeyboardEvent::KeyCodeType code_type,
                     const KeyboardEvent::KeyEventType event_type) {
        KeyCodeType type = static_cast<KeyCodeType>(code_type);
        QKeyCode qcode = keycode_to_qcode(code, type);
        if (qcode >= Q_KEY_CODE__MAX) {
            LOG(FATAL) << "Invalid QKeyCode - too big: " << qcode;
        }
        if (event_type == KeyboardEvent::keydown || event_type == KeyboardEvent::keypress) {
            qemu_loop_
                    ->Post([qcode, kbd = keyboard_state_] {
                        QemuKeyEvent press_event{qcode, true};
                        press_event.send(kbd);
                    })
                    .IgnoreError();
        }
        if (event_type == KeyboardEvent::keyup || event_type == KeyboardEvent::keypress) {
            qemu_loop_
                    ->Post([qcode, kbd = keyboard_state_] {
                        QemuKeyEvent release_event{qcode, false};
                        release_event.send(kbd);
                    })
                    .IgnoreError();
        }
    }

    void DispatchKeyboardEvent(const KeyboardEvent request) {
        VLOG(1) << "Handling " << request.ShortDebugString();
        if (request.key().size() > 0) {
            keyboard::DomKey domkey = BrowserKeyToDomKey(request.key());
            if (domkey != keyboard::DomKey::NONE) {
                // okay, check if it is a non printable char:
                keyboard::DomCode code = DomKeyAsNonPrintableDomCode(domkey);
                if (code == keyboard::DomCode::NONE) {
                    // We are sending an individual key.
                    auto character = domkey.ToCharacter();
                    if (character > 0xFF) {
                        // Ignore non ascii..
                        VLOG(1) << "Ignoring non ascii character, we cannot translate it Key="
                                << domkey.ToUtf8();
                        return;
                    }
                    auto event_type = request.eventtype();
                    if (event_type == KeyboardEvent::keydown ||
                        event_type == KeyboardEvent::keypress) {
                        for (auto keycode : ascii_to_qcode(character, true)) {
                            qemu_loop_
                                    ->Post([key = keycode, kbd = keyboard_state_] {
                                        key.send(kbd);
                                    })
                                    .IgnoreError();
                        }
                    }
                    if (event_type == KeyboardEvent::keyup ||
                        event_type == KeyboardEvent::keypress) {
                        for (auto keycode : ascii_to_qcode(character, false)) {
                            qemu_loop_
                                    ->Post([key = keycode, kbd = keyboard_state_] {
                                        key.send(kbd);
                                    })
                                    .IgnoreError();
                        }
                    }
                } else {
                    // Nope we have to send the domcode..
                    SendKeyCode(dom_to_evdev(code), KeyboardEvent::Evdev, request.eventtype());
                }
            }
        }

        if (request.text().size() > 0) {
            SendUtf8String(request.text());
        }

        if (request.keycode() > 0) {
            SendKeyCode(request.keycode(), request.codetype(), request.eventtype());
        }
    }

    void SendUtf8String(const std::string& utf8) {
        VLOG(1) << "Sending utf8 string: " << utf8;
        // We need to convert every individual character to a sequence of evdev
        // events. This is due to the fact that a single character can be
        // translated into multiple ev dev events in case a modifier is needed.
        // For example: 'e' -> gives one ev dev event [18]. 'E' ->  gives two
        // [42, 18] (shift + e). The end result is that the sequence "Ee" will be
        // translated as [42, 18, 18]. If we deliver this sequence as down + up, we
        // would see "EE" in android. By splitting this up in individual chars we
        // guarantee that this doesn't happen.
        const char* start = utf8.c_str();  // Guaranteed 0 terminated.
        const char* end = utf8.c_str();
        while (*end != '\0') {
            char ch = *end;
            end++;  // Can point to \0, but not beyond \0

            // For utf-8 we have that if the two high bits set to 10, it's a
            // continuation byte. What we are doing here is checking if the first
            // two bits are not 10 and hence indicate a new character. This means
            // we have found a single utf-8 char between start and end (of at most 4
            // bytes).
            if ((ch & 0xc0) != 0x80) {
                DCHECK(start < end);
                DCHECK(end - start <= 4);
                auto len = end - start;
                if (len > 1) {
                    LOG(ERROR) << "The utf-8 char: " << std::string_view(start, len)
                               << " will not be translated.";
                }
                auto down = ascii_to_qcode(*start, true);
                auto up = ascii_to_qcode(*start, false);

                for (auto qcode : down) {
                    qemu_loop_->Post([qcode, kbd = keyboard_state_] { qcode.send(kbd); })
                            .IgnoreError();
                }
                for (auto qcode : up) {
                    qemu_loop_->Post([qcode, kbd = keyboard_state_] { qcode.send(kbd); })
                            .IgnoreError();
                }
                start = end;
            }
        }
    }

    DomCode DomKeyAsNonPrintableDomCode(DomKey key) {
        for (size_t i = 0; i < kNonPrintableCodeEntries; ++i) {
            if (kNonPrintableCodeMap[i].dom_key == key) {
                return kNonPrintableCodeMap[i].dom_code;
            }
        }
        return DomCode::NONE;
    }

    DomKey BrowserKeyToDomKey(std::string key) {
        if (key.empty()) {
            return DomKey::NONE;
        }

        // Check for standard key names.
        for (size_t i = 0; i < kDomKeyMapEntries; ++i) {
            if (dom_key_map[i].string && key == dom_key_map[i].string) {
                return dom_key_map[i].dom_key;
            }
        }
        if (key == "Dead") {
            // A dead "combining" key; that is, a key which is used in
            // tandem with other keys to generate accented and other
            // modified characters. If pressed by itself, it doesn't
            // generate a character. If you wish to identify which specific
            // dead key was pressed (in cases where more than one exists),
            // you can do so by examining the KeyboardEvent's associated
            // compositionupdate event's  data property.
            return DomKey::DeadKeyFromCombiningCharacter(0xFFFF);
        }
        // Otherwise, if the string contains a single character,
        // the key value is that character.
        if (key.size() == 1) {
            return DomKey::FromCharacter(key);
        }

        return DomKey::NONE;
    }

    EventLoop* qemu_loop_;
    ::QKbdState* keyboard_state_;
};

std::unique_ptr<IKeyEventSender> createKeyEventSender(QemuConsole* console, EventLoop* qemu_loop) {
    return std::make_unique<KeyEventSenderImpl>(console, qemu_loop);
}

}  // namespace keyboard
}  // namespace control
}  // namespace emulation
}  // namespace android
