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
#include "absl/log/log.h"

#include "android/emulation/control/keyboard/key_conversion.h"
extern "C" QKbdState* qkbd_state_init(QemuConsole* con);
extern "C" void qkbd_state_key_event(QKbdState* kbd, int qcode, bool down);

#include "ui/input-keymap-linux-to-qcode.c.inc"
#include "ui/input-keymap-qcode-to-linux.c.inc"

namespace android {
namespace emulation {
namespace control {
namespace keyboard {

typedef struct SkinKeyEntry {
    QKeyCode code;
    unsigned short base;
    unsigned short caps;
    unsigned short fn;
    unsigned short caps_fn;
    unsigned short number;
} SkinKeyEntry;

static const SkinKeyEntry cmap[] = {
        /* keycode                   base   caps    fn  caps+fn   number */

        {Q_KEY_CODE_A, 'a', 'A', 0xe1, 0xc1, 'a'},
        {Q_KEY_CODE_B, 'b', 'B', 'b', 'B', 'b'},
        {Q_KEY_CODE_C, 'c', 'C', 0xa9, 0xa2, 'c'},
        {Q_KEY_CODE_D, 'd', 'D', 0xf0, 0xd0, '\''},
        {Q_KEY_CODE_E, 'e', 'E', 0xe9, 0xc9, '"'},
        {Q_KEY_CODE_F, 'f', 'F', '[', '[', '['},
        {Q_KEY_CODE_G, 'g', 'G', ']', ']', ']'},
        {Q_KEY_CODE_H, 'h', 'H', '<', '<', '<'},
        {Q_KEY_CODE_I, 'i', 'I', 0xed, 0xcd, '-'},
        {Q_KEY_CODE_J, 'j', 'J', '>', '>', '>'},
        {Q_KEY_CODE_K, 'k', 'K', ';', 'K', ';'},
        {Q_KEY_CODE_L, 'l', 'L', 0xf8, 0xd8, ':'},
        {Q_KEY_CODE_M, 'm', 'M', 0xb5, 'M', '%'},
        {Q_KEY_CODE_N, 'n', 'N', 0xf1, 0xd1, 'n'},
        {Q_KEY_CODE_O, 'o', 'O', 0xf3, 0xd3, '+'},
        {Q_KEY_CODE_P, 'p', 'P', 0xf6, 0xd6, '='},
        {Q_KEY_CODE_Q, 'q', 'Q', 0xe4, 0xc4, '|'},
        {Q_KEY_CODE_R, 'r', 'R', 0xae, 'R', '`'},
        {Q_KEY_CODE_S, 's', 'S', 0xdf, 0xa7, '\\'},
        {Q_KEY_CODE_T, 't', 'T', 0xfe, 0xde, '}'},
        {Q_KEY_CODE_U, 'u', 'U', 0xfa, 0xda, '_'},
        {Q_KEY_CODE_V, 'v', 'V', 'v', 'V', 'v'},
        {Q_KEY_CODE_W, 'w', 'W', 0xe5, 0xc5, '~'},
        {Q_KEY_CODE_X, 'x', 'X', 'x', 'X', 'x'},
        {Q_KEY_CODE_Y, 'y', 'Y', 0xfc, 0xdc, '}'},
        {Q_KEY_CODE_Z, 'z', 'Z', 0xe6, 0xc6, 'z'},
        {Q_KEY_CODE_COMMA, ',', '<', 0xe7, 0xc7, ','},
        {Q_KEY_CODE_DOT, '.', '>', '.', 0x2026, '.'},
        {Q_KEY_CODE_SLASH, '/', '?', 0xbf, '?', '/'},
        {Q_KEY_CODE_SPC, 0x20, 0x20, 0x9, 0x9, 0x20},
        {Q_KEY_CODE_LF, 0xa, 0xa, 0xa, 0xa, 0xa},
        {Q_KEY_CODE_0, '0', ')', 0x2bc, ')', '0'},
        {Q_KEY_CODE_1, '1', '!', 0xa1, 0xb9, '1'},
        {Q_KEY_CODE_2, '2', '@', 0xb2, '@', '2'},
        {Q_KEY_CODE_3, '3', '#', 0xb3, '#', '3'},
        {Q_KEY_CODE_4, '4', '$', 0xa4, 0xa3, '4'},
        {Q_KEY_CODE_5, '5', '%', 0x20ac, '%', '5'},
        {Q_KEY_CODE_6, '6', '^', 0xbc, 0x0302, '6'},
        {Q_KEY_CODE_7, '7', '&', 0xbd, '&', '7'},
        {Q_KEY_CODE_8, '8', '*', 0xbe, '*', '8'},
        {Q_KEY_CODE_9, '9', '(', 0x2bb, '(', '9'},
        {Q_KEY_CODE_TAB, 0x9, 0x9, 0x9, 0x9, 0x9},
        {Q_KEY_CODE_GRAVE_ACCENT, '`', '~', 0x300, 0x0303, '`'},
        {Q_KEY_CODE_MINUS, '-', '_', 0xa5, '_', '-'},
        {Q_KEY_CODE_EQUAL, '=', '+', 0xd7, 0xf7, '='},
        {Q_KEY_CODE_BRACKET_LEFT, '[', '{', 0xab, '{', '['},
        {Q_KEY_CODE_BRACKET_RIGHT, ']', '}', 0xbb, '}', ']'},
        {Q_KEY_CODE_BACKSLASH, '\\', '|', 0xac, 0xa6, '\\'},
        {Q_KEY_CODE_SEMICOLON, ';', ':', 0xb6, 0xb0, ';'},
        {Q_KEY_CODE_APOSTROPHE, '\'', '"', 0x301, 0x308, '\''},
};

QKeyCode evdev_to_qcode(uint32_t evdev) {
    if (evdev >= qemu_input_map_linux_to_qcode_len) {
        VLOG(1) << "evdev: " << std::hex << evdev << std::dec << " is out of range ("
                << qemu_input_map_linux_to_qcode_len << ")";
        return (QKeyCode)0;
    }
    return (QKeyCode)qemu_input_map_linux_to_qcode[evdev];
}

uint32_t qcode_to_evdev(QKeyCode qcode) {
    if (qcode >= qemu_input_map_qcode_to_linux_len) {
        return 0;
    }
    return (uint32_t)qemu_input_map_qcode_to_linux[qcode];
}

std::vector<QemuKeyEvent> ascii_to_qcode(unsigned short unicode, bool down) {
    /* check base keys */
    for (int n = 0; n < sizeof(cmap); n++) {
        if (cmap[n].base == unicode) {
            return {{cmap[n].code, down}};
        }
    }

    /* check caps + keys */
    for (int n = 0; n < sizeof(cmap); n++) {
        if (cmap[n].caps == unicode) {
            if (down) {
                return {{Q_KEY_CODE_SHIFT, down}, {cmap[n].code, down}};
            }
            return {{cmap[n].code, down}, {Q_KEY_CODE_SHIFT, down}};
        }
    }

    /* check fn + keys */
    for (int n = 0; n < sizeof(cmap); n++) {
        if (cmap[n].fn == unicode) {
            if (down) {
                return {{Q_KEY_CODE_ALT, down}, {cmap[n].code, down}};
            }
            return {{cmap[n].code, down}, {Q_KEY_CODE_ALT, down}};
        }
    }

    /* check caps + fn + keys */
    for (int n = 0; n < sizeof(cmap); n++) {
        if (cmap[n].caps_fn == unicode) {
            if (down) {
                return {{Q_KEY_CODE_SHIFT, down}, {Q_KEY_CODE_ALT, down}, {cmap[n].code, down}};
            }

            return {{cmap[n].code, down}, {Q_KEY_CODE_ALT, down}, {Q_KEY_CODE_SHIFT, down}};
        }
    }
    return {};
}

void QemuKeyEvent::send(QKbdState* kbd) {
    VLOG(1) << "Sending: " << code << " " << (down ? "down" : "up");
    qkbd_state_key_event(kbd, code, down);
}

#define USB_KEYMAP(usb, evdev, xkb, win, mac, code, id) \
    {DomCode::id, usb, evdev, xkb, win, mac, code}
#define USB_KEYMAP_DECLARATION static const std::vector<KeycodeMapEntry> usb_keycode_map =
#include "keycode_converter_data.inc"

#undef USB_KEYMAP
#undef USB_KEYMAP_DECLARATION

const std::vector<KeycodeMapEntry>& keymap() {
    return usb_keycode_map;
}

QKeyCode dom_to_qcode(DomCode key) {
    return evdev_to_qcode(dom_to_evdev(key));
}

uint32_t dom_to_evdev(DomCode key) {
    for (const auto entry : usb_keycode_map) {
        if (entry.id == key) {
            return entry.evdev;
        }
    }
    return 0;
}

uint32_t keycode_to_evdev(uint32_t from, KeyCodeType source) {
    for (const auto& entry : usb_keycode_map) {
        switch (source) {
            case KeyCodeType::usb:
                if (entry.usb == from) return entry.evdev;
                break;
            case KeyCodeType::evdev:
                if (entry.evdev == from) return entry.evdev;
                break;
            case KeyCodeType::xkb:
                if (entry.xkb == from) return entry.evdev;
                break;
            case KeyCodeType::win:
                if (entry.win == from) return entry.evdev;
                break;
            case KeyCodeType::mac:
                if (entry.mac == from) return entry.evdev;
                break;
        }
    }
    return 0;
}

QKeyCode keycode_to_qcode(uint32_t from, KeyCodeType source) {
    return evdev_to_qcode(keycode_to_evdev(from, source));
}
}  // namespace keyboard
}  // namespace control
}  // namespace emulation
}  // namespace android