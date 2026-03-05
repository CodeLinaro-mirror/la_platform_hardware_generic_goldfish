
// Copyright (C) 2023 The Android Open Source Project
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
#pragma once

extern "C" {
// clang-format off
// IWYU pragma: begin_keep

#include "qemu/osdep.h"
#include "ui/input.h"

typedef struct QKbdState QKbdState;
// IWYU pragma: end_keep
// clang-format on
}

#include <cstdint>
#include <vector>

#include "emulator/grpc/services/emulator_controller/keyboard/dom_key.h"

// Necessary on Windows.
#undef send

namespace android {
namespace emulation {
namespace control {
namespace keyboard {

/**
 * @struct QemuKeyEvent
 * @brief Represents a single keyboard event for QEMU.
 *
 * This structure encapsulates a QEMU keycode and the state of the key
 * (pressed or released).
 */
struct QemuKeyEvent {
    /**
     * @brief Sends the keyboard event to the QEMU keyboard state.
     *
     * @note This method should be invoked on the qemu event loop.
     * @param kbd A pointer to the QEMU keyboard state.
     */
    void send(QKbdState* kbd) const;

    /**
     * @brief The QEMU keycode.
     */
    QKeyCode code;

    /**
     * @brief True if the key is pressed (key down), false if released (key up).
     */
    bool down;
};

/**
 * @enum KeyCodeType
 * @brief Enumerates the different types of keycode representations.
 */
enum class KeyCodeType : uint8_t {
    usb = 0,    ///< USB HID keycode.
    evdev = 1,  ///< Linux evdev keycode.
    xkb = 2,    ///< Linux XKB keycode.
    win = 3,    ///< Windows OEM scan code.
    mac = 4,    ///< Mac keycode
};

/**
 * @brief Represents a mapping between different keycode representations.
 *
 * This structure defines a mapping between a DOM Code and its corresponding
 * representations in USB HID, evdev, XKB, Windows, and Mac keycode systems.
 */
struct KeycodeMapEntry {
    /**
     * @brief The DOM Code (UIEvents code value).
     *
     * @see http://www.w3.org/TR/DOM-Level-3-Events-code/
     */
    DomCode id;

    /**
     * @brief USB HID keycode.
     *
     * Upper 16-bits: USB Usage Page.
     * Lower 16-bits: USB Usage Id (assigned ID within this usage page).
     */
    uint32_t usb;

    /**
     * @brief evdev keycode.
     */
    uint32_t evdev;

    /**
     * @brief Linux XKB keycode.
     */
    uint32_t xkb;

    /**
     * @brief Windows OEM scan code.
     */
    uint32_t win;

    /**
     * @brief Mac keycode.
     */
    uint32_t mac;

    /**
     * @brief The UIEvents (aka: DOM4Events) |code| value as defined in:
     * http://www.w3.org/TR/DOM-Level-3-Events-code/
     */
    const char* code;
};

/**
 * @brief Returns the keymap, a vector of KeycodeMapEntry.
 *
 * @return A constant reference to the keymap vector.
 */
const std::vector<KeycodeMapEntry>& keymap();

/**
 * @brief Converts an evdev keycode to a QEMU keycode.
 *
 * @param evdev The evdev keycode to convert.
 * @return The corresponding QEMU keycode.
 */
QKeyCode evdev_to_qcode(uint32_t evdev);

/**
 * @brief Converts a DOM Code to a QEMU keycode.
 *
 * @param key The DOM Code to convert.
 * @return The corresponding QEMU keycode.
 */
QKeyCode dom_to_qcode(DomCode key);

/**
 * @brief Converts a keycode of a given type to a QEMU keycode.
 *
 * @param from The keycode to convert.
 * @param source The type of the keycode.
 * @return The corresponding QEMU keycode.
 */
QKeyCode keycode_to_qcode(uint32_t from, KeyCodeType source);

/**
 * @brief Translates an ASCII key event into a sequence of QEMU key events.
 *
 * This function handles modifier keys (e.g., Shift, Alt) as needed to
 * represent the given ASCII character.
 *
 * @param ascii The ASCII character to translate.
 * @param down True if the key is pressed, false if released.
 * @return A vector of QemuKeyEvent representing the key event sequence.
 */
std::vector<QemuKeyEvent> ascii_to_qcode(unsigned short ascii, bool down);

/**
 * @brief Converts a DOM Code to an evdev keycode.
 *
 * @param key The DOM Code to convert.
 * @return The corresponding evdev keycode.
 */
uint32_t dom_to_evdev(DomCode key);

/**
 * @brief Converts a keycode of a given type to an evdev keycode.
 *
 * @param from The keycode to convert.
 * @param source The type of the keycode.
 * @return The corresponding evdev keycode.
 */
uint32_t keycode_to_evdev(uint32_t from, KeyCodeType source);

}  // namespace keyboard
}  // namespace control
}  // namespace emulation
}  // namespace android
