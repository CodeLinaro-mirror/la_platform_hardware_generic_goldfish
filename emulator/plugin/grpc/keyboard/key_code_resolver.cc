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

#include "android/emulation/control/keyboard/key_conversion.h"

namespace android::emulation::control::keyboard {

using ::android::emulation::v2::KeyEvent;

uint32_t ResolveEvdevKeyCode(const KeyEvent& key) {
    switch (key.key_identifier_case()) {
    case KeyEvent::kDomCode:
        return dom_code_to_evdev(key.dom_code());
    case KeyEvent::kAndroidKeycode:
        return android_keycode_to_evdev(key.android_keycode());
    case KeyEvent::kEvdevCode:
        return key.evdev_code() > 0 ? static_cast<uint32_t>(key.evdev_code()) : 0U;
    case KeyEvent::KEY_IDENTIFIER_NOT_SET:
        break;
    }
    return 0;
}

}  // namespace android::emulation::control::keyboard
