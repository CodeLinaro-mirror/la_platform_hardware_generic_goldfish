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
#pragma once

#include <cstdint>

#include "input_service.pb.h"

namespace android::emulation::control::keyboard {

/**
 * @brief Maps the key identifier of `key` to a Linux evdev code.
 *
 * Resolves a W3C DOM code through `dom_code_to_evdev()`, an Android keycode
 * through `android_keycode_to_evdev()`, and a positive evdev code as is.
 *
 * @param key The v2 KeyEvent to resolve.
 * @return The evdev `KEY_*` code, or 0 if `key` has no identifier or it names
 *     no supported key.
 */
uint32_t ResolveEvdevKeyCode(const ::android::emulation::v2::KeyEvent& key);

}  // namespace android::emulation::control::keyboard
