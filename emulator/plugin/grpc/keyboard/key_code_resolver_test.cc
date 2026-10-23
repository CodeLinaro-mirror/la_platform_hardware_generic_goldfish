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

#include "android/emulation/control/keyboard/key_code_resolver.h"

#include <gtest/gtest.h>

#include "input_service.pb.h"
#include "standard-headers/linux/input-event-codes.h"

namespace android::emulation::control::keyboard {
namespace {

using ::android::emulation::v2::KeyEvent;

TEST(KeyCodeResolverTest, ResolvesDomCode) {
    KeyEvent key;
    key.set_dom_code("KeyA");
    EXPECT_EQ(ResolveEvdevKeyCode(key), static_cast<uint32_t>(KEY_A));
}

TEST(KeyCodeResolverTest, ResolvesAndroidKeycode) {
    KeyEvent key;
    key.set_android_keycode(3);  // AKEYCODE_HOME
    EXPECT_EQ(ResolveEvdevKeyCode(key), static_cast<uint32_t>(KEY_HOMEPAGE));
}

TEST(KeyCodeResolverTest, PassesEvdevCodeThrough) {
    KeyEvent key;
    key.set_evdev_code(KEY_POWER);
    EXPECT_EQ(ResolveEvdevKeyCode(key), static_cast<uint32_t>(KEY_POWER));
}

TEST(KeyCodeResolverTest, UnmappedIdentifiersResolveToZero) {
    KeyEvent dom_code;
    dom_code.set_dom_code("NonExistentKey");
    EXPECT_EQ(ResolveEvdevKeyCode(dom_code), 0U);

    KeyEvent android_keycode;
    android_keycode.set_android_keycode(9999);
    EXPECT_EQ(ResolveEvdevKeyCode(android_keycode), 0U);

    KeyEvent evdev_code;
    evdev_code.set_evdev_code(-1);
    EXPECT_EQ(ResolveEvdevKeyCode(evdev_code), 0U);
}

TEST(KeyCodeResolverTest, EmptyKeyResolvesToZero) {
    EXPECT_EQ(ResolveEvdevKeyCode(KeyEvent()), 0U);

    KeyEvent empty_dom_code;
    empty_dom_code.set_dom_code("");
    EXPECT_EQ(ResolveEvdevKeyCode(empty_dom_code), 0U);
}

}  // namespace
}  // namespace android::emulation::control::keyboard
